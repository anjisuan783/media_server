//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.


#ifndef __MEDIA_HTTP_PROTOCAL_IMPL_H__
#define __MEDIA_HTTP_PROTOCAL_IMPL_H__

#include <memory>
#include <functional>

#include "utils/media_checker.h"

#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "http/http_stack.h"
#include "common/media_consts.h"
#include "common/media_io.h"

namespace ma {

class HttpRequestReader;
class HttpResponseWriterWrapper;
class HttpResponseReader;
class MessageChain;

class AsyncSokcetWrapper : public TransportSink {
 public:
  AsyncSokcetWrapper(std::shared_ptr<Transport>);
  ~AsyncSokcetWrapper();

  void Open(bool);
  void Close();
  
  void SetReqReader(HttpRequestReader* r) {
    req_reader_ = r;
  }
  
  void SetWriter(HttpResponseWriterWrapper* w) {
    writer_ = w;
  }

  void SetResReader(HttpResponseReader* r) {
    res_reader_ = r;
  }

  srs_error_t Write(MessageChain* data, int* sent);

	void OnRead(MessageChain &msg) override;
	void OnWrite() override;
	void OnClose(srs_error_t reason) override;

  std::string Ip();
 private:
  std::shared_ptr<Transport> conn_;
  bool close_{false};

  HttpRequestReader* req_reader_ = nullptr;
  HttpResponseWriterWrapper* writer_ = nullptr;
  HttpResponseReader* res_reader_ = nullptr;
  bool server_{true};
  bool blocked_{false};
  SequenceChecker thread_check_;
};

/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * Returning `2` from on_headers_complete will tell parser that it should not
 * expect neither a body nor any futher responses on this connection. This is
 * useful for handling responses to a CONNECT request which may not contain
 * `Upgrade` or `Connection: upgrade` headers.
 *
 * http_data_cb does not return data chunks. It will be called arbitrarily
 * many times for each string. E.G. you might get 10 callbacks for "on_url"
 * each providing just a few characters more data.
 */

class HttpMessageParser final  : public IHttpMessageParser {
  
  // The state of HTTP message
  enum SrsHttpParseState {
    SrsHttpParseStateInit = 0,
    SrsHttpParseStateStart,
    SrsHttpParseStateHeaderComplete,
    SrsHttpParseStateBody,
    SrsHttpParseStateMessageComplete
  };
  
 public:
  HttpMessageParser() = default;
  ~HttpMessageParser() = default;
  
  // initialize the http parser with specified type,
  // one parser can only parse request or response messages.
  void initialize(enum http_parser_type type) override;
  
  // Whether allow jsonp parser, which indicates the method in query string.
  void set_jsonp(bool allow_jsonp) override;
 
  srs_error_t parse_message(std::string_view str_msg, 
                            std::shared_ptr<ISrsHttpMessage>&) override;
 private:
  // parse the HTTP message to member field: msg.
  srs_error_t parse_message_imp(std::string_view msg);
  
  static int on_message_begin(http_parser* parser);
  static int on_headers_complete(http_parser* parser);
  static int on_message_complete(http_parser* parser);
  static int on_url(http_parser* parser, const char* at, size_t length);
  static int on_header_field(http_parser* parser, const char* at, size_t length);
  static int on_header_value(http_parser* parser, const char* at, size_t length);
  static int on_body(http_parser* parser, const char* at, size_t length);
  static int on_chunk_header(http_parser* parser);
  static int on_chunk_complete(http_parser* parser);
  
 private:
  http_parser_settings settings_;
  http_parser parser_;

  std::string_view buffer_view_;
  std::string buffer_;
  ssize_t consumed_{0};

  // Whether allow jsonp parse.
  bool jsonp_;

  std::string field_name_;
  std::string field_value_;
  SrsHttpParseState state_{SrsHttpParseStateInit};
  http_parser hp_header_;
  std::string url_;
  std::unique_ptr<SrsHttpHeader> header_;
  enum http_parser_type type_{HTTP_REQUEST};

  // Point to the start of body.
  const char* p_body_start_{nullptr};
  // To discover the length of header, point to the last few bytes in header.
  const char* p_header_tail_{nullptr};

  std::shared_ptr<HttpMessage> msg_out_;
  SequenceChecker thread_check_;
};

class HttpRequestReader final : public IHttpRequestReader {
 public:
  HttpRequestReader(AsyncSokcetWrapper* s, CallBack* callback);
  ~HttpRequestReader() override = default;

  void open() override;
  void disconnect() override;
  
  void OnRequest(std::string_view);
  void OnDisconnect();

  std::string Ip() override;
 private:
  AsyncSokcetWrapper* socket_;
  CallBack* callback_;

  SequenceChecker thread_check_;
};

class HttpResponseWriter final {
 public:
  HttpResponseWriter();
  ~HttpResponseWriter() = default;

  void open();
  
  //IHttpResponseWriter implement
  // following function can be called by only one thread
  srs_error_t final_request(MessageChain*&);
  SrsHttpHeader* header();
  srs_error_t write(MessageChain*, MessageChain*&);
  void write_header(int code);

 private:
  MessageChain* send_header(const char* data, int size);
 private:
  std::unique_ptr<SrsHttpHeader> header_;
  static constexpr int SRS_HTTP_HEADER_CACHE_SIZE{64};
  char header_cache_[SRS_HTTP_HEADER_CACHE_SIZE];

  // Reply header has been (logically) written
  bool header_wrote_{false};

  // The wroteHeader tells whether the header's been written to "the
  // wire" (or rather: w.conn.buf). this is unlike
  // (*response).wroteHeader, which tells only whether it was
  // logically written.
  bool header_sent_{false};

  // The explicitly-declared Content-Length; or -1
  int64_t content_length_{-1};
  // The number of bytes written in body
  int64_t written_{0};

  // The status code passed to WriteHeader
  int status_{SRS_CONSTS_HTTP_OK};

  SequenceChecker thread_check_;
};

class HttpResponseWriterWrapper : public IHttpResponseWriter {
 public:
  HttpResponseWriterWrapper(std::unique_ptr<AsyncSokcetWrapper> s, bool);
  
  ~HttpResponseWriterWrapper() override;

  void open() override;

  srs_error_t final_request() override;
  
  SrsHttpHeader* header() override;

  srs_error_t write(const char* data, int size) override;

  srs_error_t write(MessageChain* data, ssize_t* pnwrite) override;

  void write_header(int code) override;

  void OnWriteEvent();
 private:
  srs_error_t send_header();
 
  srs_error_t iowrite(MessageChain*);

  //convert to http format
  MessageChain* internal_write(MessageChain* input);

  void FileWrite(MessageChain* mc);
 private:
  std::unique_ptr<HttpResponseWriter> writer_;

  MessageChain* buffer_{nullptr};
  
  bool buffer_full_{false};

  std::unique_ptr<AsyncSokcetWrapper> socket_;
  SequenceChecker thread_check_;

  SrsFileWriter file_;
};

//TODO not implement
class HttpResponseReader final : public IHttpResponseReader {
 public:
  HttpResponseReader(AsyncSokcetWrapper* p)
    : socket_(p) {
  }
  ~HttpResponseReader() override = default;

  void open(IHttpResponseReaderSink*) override;
 private:
  AsyncSokcetWrapper* socket_;
  IHttpResponseReaderSink* sink_{nullptr};
};

} //namespace ma

#endif //!__MEDIA_HTTP_PROTOCAL_IMPL_H__

