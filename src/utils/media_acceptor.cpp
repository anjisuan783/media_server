#include "utils/media_acceptor.h"
#include "utils/media_reactor.h"
#include "utils/media_thread.h"
#include "utils/media_socket.h"
#include "common/media_log.h"

namespace ma {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

class AcceptorTcp : public Acceptor, public MediaHandler {
 public:
  AcceptorTcp() = default;
  ~AcceptorTcp() override;
  
  // Acceptor implement 
  srs_error_t Listen(AcceptorSink* sink,
		                 const MediaAddress &addr) override;

	srs_error_t Stop() override;

  // MediaHandler implement
  MEDIA_HANDLE GetHandle() const override;
  int OnInput(MEDIA_HANDLE handler = MEDIA_INVALID_HANDLE) override;
  int OnClose(MEDIA_HANDLE handler, MASK mask) override;
 private:
  int OnOutput(MEDIA_HANDLE fd = MEDIA_INVALID_HANDLE) override;
  int OnException(MEDIA_HANDLE fd = MEDIA_INVALID_HANDLE) override;

  MediaThread* worker_ = nullptr;
  AcceptorSink *sink_ = nullptr;
  MediaSocketStream sock_;
};


// class AcceptorTcp
AcceptorTcp::~AcceptorTcp() {
  Stop();
}

srs_error_t AcceptorTcp::Listen(
    AcceptorSink *sink, const MediaAddress &addr) {
  srs_error_t err = srs_success;
  if (!sink) {
    return srs_error_new(ERROR_INVALID_ARGS, "sink is null");
  }
  sink_ = sink;

  bool reuse = true;
  int ret = sock_.Open(true, addr.GetType());
  if (ret == -1) {
    sock_.Close();
    return srs_error_new(ERROR_SOCKET_ERROR, 
        "sock.Open() failed! addr:%s, err:%s",
        addr.ToString().c_str(), GetSystemErrorInfo(errno).c_str());
  }

  ret = ::bind((MEDIA_HANDLE)sock_.GetHandle(), 
      reinterpret_cast<const struct sockaddr*>(addr.GetPtr()), 
      addr.GetSize());

  if (ret == -1) {
    sock_.Close();
    return srs_error_new(ERROR_SOCKET_ERROR, 
        "bind() failed! addr:%s, err:%s",
        addr.ToString().c_str(), GetSystemErrorInfo(errno).c_str());
  }

  ret = ::listen((MEDIA_HANDLE)sock_.GetHandle(), 1024);

  if (ret == -1) {
    sock_.Close();
    return srs_error_new(ERROR_SOCKET_ERROR, 
        "listen() failed! addr:%s, err:%s",
        addr.ToString().c_str(), GetSystemErrorInfo(errno).c_str());
  }

  worker_ = MediaThreadManager::Instance()->CurrentThread();

  err = worker_->Reactor()->RegisterHandler(this, MediaHandler::ACCEPT_MASK);
  if (ERROR_SUCCESS != err) {
    sock_.Close();
    return srs_error_wrap(err, "RegisterHandler faild.");
  }
  
  return err;
}

srs_error_t AcceptorTcp::Stop() {
  srs_error_t err = srs_success;
  if (sock_.GetHandle() != MEDIA_INVALID_HANDLE) {
    if (worker_) {
      err = worker_->Reactor()->RemoveHandler(this);
      worker_ = nullptr;
    }
    sock_.Close();
  }
  sink_ = nullptr;
  return err;
}

MEDIA_HANDLE AcceptorTcp::GetHandle() const {
  return sock_.GetHandle();
}

int AcceptorTcp::OnInput(MEDIA_HANDLE handler) {
  MA_ASSERT(handler == GetHandle());

  std::shared_ptr<Transport> transport = 
      TransportFactory::CreateTransport(worker_, true);

  MediaAddress peer_addr;
  int addr_len = peer_addr.GetSize();
  MEDIA_HANDLE new_sock = (MEDIA_HANDLE)::accept(
    (MEDIA_HANDLE)GetHandle(), 
    reinterpret_cast<struct sockaddr *>(const_cast<struct sockaddr_in *>(peer_addr.GetPtr())), 
    reinterpret_cast<socklen_t*>(&addr_len));

  if (new_sock == MEDIA_INVALID_HANDLE) {
    MLOG_ERROR_THIS("accept() failed! err=" << GetSystemErrorInfo(errno));
    return 0;
  }

  transport->SetSocketHandler(new_sock);

  MLOG_TRACE_THIS("addr=" << peer_addr.ToString() <<
            " port=" << peer_addr.GetPort() << 
            " fd=" << new_sock << 
            " transport=" << transport.get());

  if (sink_)
    sink_->OnAccept(std::move(transport));
  return 0;
}

int AcceptorTcp::OnClose(MEDIA_HANDLE handler, MASK mask) {
  MLOG_ERROR_THIS("can't reach here! handler:" << handler <<
    " mask=" << mask);
  return 0;
}

int AcceptorTcp::OnOutput(MEDIA_HANDLE) {
  MA_ASSERT(false);
  return -1;
}

int AcceptorTcp::OnException(MEDIA_HANDLE) {
  MA_ASSERT(false);
  return -1;
}

// AcceptorFactory
std::shared_ptr<Acceptor> AcceptorFactory::CreateAcceptor(bool tcp) {
  std::shared_ptr<Acceptor> result;
  if (tcp) {
    result = std::make_shared<AcceptorTcp>();
  }

  return std::dynamic_pointer_cast<Acceptor>(result);
}

} //namespace ma