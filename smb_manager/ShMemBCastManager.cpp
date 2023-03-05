#include "ShMemBCastManager.h"

#include <core/dispatcher/DispatcherBase.h>
#include <core/dispatcher/SelectDispatcher.h>
#include <core/link/DatagramBoard.h>
#include <core/link/UnixServerLink.h>

#include <core/link/UnixSocketUtil.h>

#include <list>

#include <pwd.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace {
// Documented, but not implemented in glibc yet
mode_t _getumask(void) {
  mode_t mask = umask(0);
  umask(mask);
  return mask;
}
} // namespace

const struct timeval ShMemBCastManager::Client::TIMEOUT = {1 * 60, 0};

const char *const ShMemBCastManager::LOG_TIME_FORMAT = "%F %T";
const char *const ShMemBCastManager::LOG_TIME_ERROR = "UNKNOWN TIME";

int ShMemBCastManager::Client::sendApprovalDenialMessage(bool approval) {
  if (approval) {
    char buffer[sizeof(Protocol::ApprovalMessage)];
    ssize_t size;

    // generate approval message
    if (0 > (size = Protocol::ApprovalMessage::init(buffer, sizeof(buffer)))) {
      return -1;
    }

    // send
    if (size != m_link.write(buffer, size)) {
      return -1;
    }
  } else {
    char buffer[sizeof(Protocol::DenialMessage)];
    ssize_t size;

    // generate denial message
    if (0 > (size = Protocol::DenialMessage::init(buffer, sizeof(buffer)))) {
      return -1;
    }

    // send
    if (size != m_link.write(buffer, size)) {
      return -1;
    }
  }

  return 0;
}

/***
 * Event Mode is unsupported for now
 */
int ShMemBCastManager::Client::handleMessage(
    Protocol::EventModeRequest *request) {
  m_eventMode = true;

  if (0 != sendApprovalDenialMessage(true)) {
    return -1;
  }

  return 0;
}

int ShMemBCastManager::Client::handleMessage(
    Protocol::NoEventModeRequest *request) {
  m_eventMode = false;

  if (0 != sendApprovalDenialMessage(true)) {
    return -1;
  }

  return 0;
}

int ShMemBCastManager::Client::handleMessage(
    Protocol::WriterSubscribeRequest *request) {
  // variables
  size_t channelNameLength;
  Channel *channel;
  int retVal = 0;

  // nul-terminate the request
  channelNameLength =
      Protocol::WriterSubscribeRequest::channelNameLength(*request);
  request->m_channelName[channelNameLength] = '\0';

  // add subscriber
  channel = m_manager->subscribe(this, request->m_channelName, true,
                                 request->m_requestedSize);
  if (0 == channel) {
    goto SUBSCRIBE_ERROR;
  }

  // add subscription to list
  Subscription subscription;
  subscription.m_channelName = channel->m_name;
  subscription.m_writer = true;

  try {
    m_subscriptions.push_back(subscription);
  } catch (const std::bad_alloc &exception) {
    // log this event
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream)) << "Could not add new subscription to "
                                << "subscription list" << std::endl;
    goto PUSH_BACK_ERROR;
  }

  // send approval
  if (0 != sendApprovalDenialMessage(true)) {
    goto SEND_APPROVAL_ERROR;
  }

  // send file descriptor
  if (0 != UnixSocketUtil::sendFd(m_link.fileDescriptor(), channel->m_fd)) {
    goto SEND_FD_ERROR;
  }

  // log the subscription
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " successfully subscribed to channel \""
      << request->m_channelName << "\" as a writer" << std::endl;

  return 0;

SEND_FD_ERROR:
SEND_APPROVAL_ERROR:
  retVal = -1;

PUSH_BACK_ERROR:
  if ((0 == retVal) &&
      (0 != m_manager->unsubscribe(this, request->m_channelName, true))) {
    // huh?!
    retVal = -1;
  }

SUBSCRIBE_ERROR:
  if ((0 == retVal) && (0 != sendApprovalDenialMessage(false))) {
    retVal = -1;
  }

  // log the subscription attempt
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " failed to subscribe to channel \""
      << request->m_channelName << "\" as a writer" << std::endl;

  return retVal;
}

int ShMemBCastManager::Client::handleMessage(
    Protocol::ReaderSubscribeRequest *request) {
  // variables
  size_t channelNameLength;
  Channel *channel;
  int retVal = 0;

  // nul-terminate the request
  channelNameLength =
      Protocol::ReaderSubscribeRequest::channelNameLength(*request);
  request->m_channelName[channelNameLength] = '\0';

  // add subscriber
  channel = m_manager->subscribe(this, request->m_channelName, false,
                                 request->m_requestedSize);
  if (0 == channel) {
    goto SUBSCRIBE_ERROR;
  }

  // add subscription to list
  Subscription subscription;
  subscription.m_channelName = channel->m_name;
  subscription.m_writer = false;

  try {
    m_subscriptions.push_back(subscription);
  } catch (const std::bad_alloc &exception) {
    // log this event
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream)) << "Could not add new subscription to "
                                << "subscription list" << std::endl;
    goto PUSH_BACK_ERROR;
  }

  // send approval
  if (0 != sendApprovalDenialMessage(true)) {
    goto SEND_APPROVAL_ERROR;
  }

  // send file descriptor
  if (0 != UnixSocketUtil::sendFd(m_link.fileDescriptor(), channel->m_fd)) {
    goto SEND_FD_ERROR;
  }

  // log the subscription attempt
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " successfully subscribed to channel \""
      << request->m_channelName << "\" as a reader" << std::endl;

  return 0;

SEND_FD_ERROR:
SEND_APPROVAL_ERROR:
  retVal = -1;

PUSH_BACK_ERROR:
  if ((0 == retVal) &&
      (0 != m_manager->unsubscribe(this, request->m_channelName, false))) {
    // huh?!
    retVal = -1;
  }

SUBSCRIBE_ERROR:
  if ((0 == retVal) && (0 != sendApprovalDenialMessage(false))) {
    retVal = -1;
  }

  // log the subscription attempt
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " failed to subscribe to channel \""
      << request->m_channelName << "\" as a reader" << std::endl;

  return retVal;
}

int ShMemBCastManager::Client::handleMessage(
    Protocol::WriterUnsubscribeRequest *request) {
  // variables
  size_t channelNameLength;
  std::list<Subscription>::iterator iter;
  int retVal = 0;

  // nul-terminate the request
  channelNameLength =
      Protocol::WriterUnsubscribeRequest::channelNameLength(*request);
  request->m_channelName[channelNameLength] = '\0';

  // find the subscription from list
  for (iter = m_subscriptions.begin(); iter != m_subscriptions.end(); iter++) {
    if (((*iter).m_writer) &&
        (0 == ::strcmp((*iter).m_channelName, request->m_channelName))) {
      // found!
      break;
    }
  }

  if (iter == m_subscriptions.end()) {
    // could not find the subscription
    goto NO_SUCH_SUBSCRIPTION_ERROR;
  }

  // attempt to remove Subscriber
  if (0 != m_manager->unsubscribe(this, request->m_channelName, true)) {
    goto UNSUBSCRIBE_ERROR;
  }

  // remove the subscription
  m_subscriptions.erase(iter);

  // send approval
  if (0 != sendApprovalDenialMessage(true)) {
    goto SEND_APPROVAL_ERROR;
  }

  // log event
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " successfully unsubscribed from channel \""
      << request->m_channelName << "\" as a writer" << std::endl;

  return 0;

SEND_APPROVAL_ERROR:
  retVal = -1;

UNSUBSCRIBE_ERROR:
  if ((0 == retVal) && (0 != sendApprovalDenialMessage(false))) {
    retVal = -1;
  }

NO_SUCH_SUBSCRIPTION_ERROR:
  // log unsubscription attempt
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " failed to unsubscribe from channel \""
      << request->m_channelName << "\" as a writer" << std::endl;

  return retVal;
}

int ShMemBCastManager::Client::handleMessage(
    Protocol::ReaderUnsubscribeRequest *request) {
  // variables
  size_t channelNameLength;
  std::list<Subscription>::iterator iter;
  int retVal = 0;

  // nul-terminate the request
  channelNameLength =
      Protocol::ReaderUnsubscribeRequest::channelNameLength(*request);
  request->m_channelName[channelNameLength] = '\0';

  // find the subscription in the list
  for (iter = m_subscriptions.begin(); iter != m_subscriptions.end(); iter++) {
    if ((!(*iter).m_writer) &&
        (0 == ::strcmp((*iter).m_channelName, request->m_channelName))) {
      // found!
      break;
    }
  }

  if (iter == m_subscriptions.end()) {
    // subscription not found
    goto NO_SUCH_SUBSCRIPTION_ERROR;
  }

  // attempt to remove Subscriber
  if (0 != m_manager->unsubscribe(this, request->m_channelName, false)) {
    goto UNSUBSCRIBE_ERROR;
  }

  // remove subscription
  m_subscriptions.erase(iter);

  // send approval
  if (0 != sendApprovalDenialMessage(true)) {
    goto SEND_APPROVAL_ERROR;
  }

  // log event
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " successfully unsubscribed from channel \""
      << request->m_channelName << "\" as a reader" << std::endl;

  return 0;

SEND_APPROVAL_ERROR:
  retVal = -1;

UNSUBSCRIBE_ERROR:
  if ((0 == retVal) && (0 != sendApprovalDenialMessage(false))) {
    retVal = -1;
  }

NO_SUCH_SUBSCRIPTION_ERROR:
  // log event
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " failed to unsubscribe from channel \""
      << request->m_channelName << "\" as a reader" << std::endl;

  return retVal;
}

void ShMemBCastManager::Client::sendChannelSubscriptionEvent(
    uint16_t numReaders, const char *channel) {
  // variables
  const size_t maxMessageSize = Protocol::Constants::MAX_MESSAGE_SIZE;
  char *buffer;
  ssize_t size;

  if (!m_eventMode) {
    // not an event mode client
    return;
  }

  buffer = new (std::nothrow) char[maxMessageSize];
  if (0 == buffer) {
    goto BUFFER_ALLOCATE_ERROR;
  }

  size = Protocol::ChannelSubscriptionEvent::init(buffer, maxMessageSize,
                                                  numReaders, channel);
  if (0 > size) {
    goto MESSAGE_INIT_ERROR;
  }

  if (size != m_link.write(buffer, size)) {
    goto WRITE_ERROR;
  }

  delete[] buffer;
  return;

WRITE_ERROR:
  // not a fatal error, as it is just an event
MESSAGE_INIT_ERROR:
  delete[] buffer;

BUFFER_ALLOCATE_ERROR:
  return;
}

int ShMemBCastManager::Client::onRead(void) {
  ssize_t bytesRead;
  char buffer[Protocol::Constants::MAX_MESSAGE_SIZE];
  Protocol::Header *header;
  int retVal;

  // read requestn
  bytesRead = m_link.read(buffer, Protocol::Constants::MAX_MESSAGE_SIZE);
  if (static_cast<ssize_t>(sizeof(Protocol::Header)) > bytesRead) {
    // Must have at least a header...
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream))
      << "Process " << m_pid << " read error" << std::endl;
    goto READ_ERROR;
  }

  // verify protocol version
  header = reinterpret_cast<Protocol::Header *>(buffer);
  if (Protocol::VERSION != header->m_version) {
    // unsupported version
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream))
      << "Process " << m_pid << " version error" << std::endl;
    goto VERSION_ERROR;
  }

  // verify request size matches up
  if (static_cast<ssize_t>(header->m_size) != bytesRead) {
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream))
      << "Process " << m_pid << " msg size error" << std::endl;
    goto MESSAGE_SIZE_ERROR;
  }

  // work based on request type. Result in a channel data
  switch (header->m_messageType) {
  case Protocol::EVENT_MODE_REQUEST: {
    retVal =
        handleMessage(reinterpret_cast<Protocol::EventModeRequest *>(buffer));
  } break;

  case Protocol::NO_EVENT_MODE_REQUEST: {
    retVal =
        handleMessage(reinterpret_cast<Protocol::NoEventModeRequest *>(buffer));
  } break;

  case Protocol::WRITER_SUBSCRIBE_REQUEST: {
    retVal = handleMessage(
        reinterpret_cast<Protocol::WriterSubscribeRequest *>(buffer));
  } break;

  case Protocol::READER_SUBSCRIBE_REQUEST: {
    retVal = handleMessage(
        reinterpret_cast<Protocol::ReaderSubscribeRequest *>(buffer));
  } break;

  case Protocol::WRITER_UNSUBSCRIBE_REQUEST: {
    retVal = handleMessage(
        reinterpret_cast<Protocol::WriterUnsubscribeRequest *>(buffer));
  } break;

  case Protocol::READER_UNSUBSCRIBE_REQUEST: {
    retVal = handleMessage(
        reinterpret_cast<Protocol::ReaderUnsubscribeRequest *>(buffer));
  } break;

  default: {
    (*(m_manager->m_logStream))
      << "Process " << m_pid << " unsupported msg" << std::endl;
    goto UNSUPPORTED_MESSAGE_ERROR;
  }
  }

  if (0 == retVal) {
    return 0;
  } else {
    disconnect();
    return TTECH_DELETE_CHAN;
  }

UNSUPPORTED_MESSAGE_ERROR:
MESSAGE_SIZE_ERROR:
VERSION_ERROR:
READ_ERROR:
  disconnect();
  return TTECH_DELETE_CHAN;
}

void ShMemBCastManager::Client::disconnect(void) {
  m_manager->printLogPrefix();
  (*(m_manager->m_logStream))
      << "Process " << m_pid << " disconnected" << std::endl;

  for (std::list<Subscription>::iterator iter = m_subscriptions.begin();
       iter != m_subscriptions.end(); iter++) {
    // log and unsubscribe
    m_manager->printLogPrefix();
    (*(m_manager->m_logStream))
        << "Process " << m_pid << " considered unsubscribed from channel \""
        << (*iter).m_channelName << "\" as a "
        << (((*iter).m_writer) ? ("writer") : ("reader")) << std::endl;

    m_manager->unsubscribe(this, (*iter).m_channelName, (*iter).m_writer);
  }
  m_subscriptions.clear();

  m_link.close();
}

ShMemBCastManager::Channel *
ShMemBCastManager::subscribe(Client *client, const char *channelName,
                             bool writer, uint32_t requestedSize) {
  // search for existing channel
  for (std::list<Channel>::iterator iter = m_channels.begin();
       iter != m_channels.end(); iter++) {
    if (0 == ::strcmp(channelName, (*iter).m_name)) {
      // found an existing channel
      if (writer) {
        // add a writer
        if (0 != (*iter).m_writer) {
          // writer already subscribed
          return 0;
        }

        (*iter).m_writer = client;
      } else {
        // add a reader
        (*iter).m_readers.push_back(client);
        if (0 != (*iter).m_writer) {
          // send a channel subscription event
          (*iter).m_writer->sendChannelSubscriptionEvent(
              (*iter).m_readers.size(), (*iter).m_name);
        }
      }

      return &(*iter);
    }
  }

  // could not find an existing channel. Create a new one.
  DatagramBoard datagramBoard;
  int boardFd;
  uint64_t boardSize;
  char *channelNameCopy;
  Channel *channel;

  // (1) create the board
  boardSize = (0 == requestedSize) ? (m_defaultBufferSize) : (requestedSize);
  if (0 != datagramBoard.create(&boardFd, boardSize)) {
    // board creation failed
    goto DATAGRAMBOARD_CREATE_ERROR;
  }
  // get real board size and unmap board as I do not need it
  boardSize = datagramBoard.m_boardInfo->m_size;
  datagramBoard.unmap();

  // (2) create copy of channel name
  channelNameCopy = new (std::nothrow) char[::strlen(channelName) + 1];
  if (0 == channelNameCopy) {
    goto CHANNEL_NAME_ALLOCATION_ERROR;
  }
  ::strcpy(channelNameCopy, channelName);

  // add channel to list of channels
  try {
    m_channels.push_back(Channel());
  } catch (const std::bad_alloc &exception) {
    // log this event
    printLogPrefix();
    (*m_logStream) << "Could not add new channel \"" << channelNameCopy
                   << "\" to channel list" << std::endl;
    goto PUSH_BACK_ERROR;
  }

  // populate channel and subscribe client
  channel = &m_channels.back();
  channel->m_name = channelNameCopy;
  channel->m_fd = boardFd;
  if (writer) {
    channel->m_writer = client;
  } else {
    channel->m_writer = 0;
    channel->m_readers.push_back(client);
  }

  // log this event
  printLogPrefix();
  (*m_logStream) << "Created channel \"" << channelNameCopy << "\" with size "
                 << boardSize << " bytes" << std::endl;

  return channel;

PUSH_BACK_ERROR:
  delete[] channelNameCopy;

CHANNEL_NAME_ALLOCATION_ERROR:
  ::close(boardFd);

DATAGRAMBOARD_CREATE_ERROR:
  return 0;
}

int ShMemBCastManager::unsubscribe(Client *client, const char *channelName,
                                   bool writer) {
  // search for existing channel
  for (std::list<Channel>::iterator channelIter = m_channels.begin();
       channelIter != m_channels.end(); channelIter++) {
    if (0 == ::strcmp(channelName, (*channelIter).m_name)) {
      // found. Remove the subscription
      if (writer) {
        if ((*channelIter).m_writer == client) {
          (*channelIter).m_writer = 0;
        } else {
          // not the writer
          return -1;
        }
      } else {
        // remove a reader
        std::list<Client *>::iterator clientIter;
        for (clientIter = (*channelIter).m_readers.begin();
             clientIter != (*channelIter).m_readers.end(); clientIter++) {
          if ((*clientIter) == client) {
            // found reader
            break;
          }
        }

        if (clientIter != (*channelIter).m_readers.end()) {
          (*channelIter).m_readers.erase(clientIter);
          if (0 != (*channelIter).m_writer) {
            // send a channel subscription event
            (*channelIter)
                .m_writer->sendChannelSubscriptionEvent(
                    (*channelIter).m_readers.size(), (*channelIter).m_name);
          }
        } else {
          // client does not have a read subscription to this channel
          return -1;
        }
      }

      // delete channel if unsubscribed
      if ((0 == (*channelIter).m_writer) && (*channelIter).m_readers.empty()) {
        // need to delete the buffer for good
        // log this event
        printLogPrefix();
        (*m_logStream) << "Destroyed channel \"" << (*channelIter).m_name
                       << "\"" << std::endl;

        delete[]((*channelIter).m_name);
        ::close((*channelIter).m_fd);
        m_channels.erase(channelIter);
      }

      return 0;
    }
  }

  // channel not found
  return -1;
}

int ShMemBCastManager::init(const std::string &vlan,
                            const std::set<uid_t> &permittedUIDSet,
                            const std::set<gid_t> &permittedGIDSet,
                            uint64_t defaultBufferSize,
                            const std::string &logFilePath) {
  std::streambuf *streambuf;
  const IPCAddress &managerAddress =
      ShMemBCastProtocol::getManagerIPCAddress(vlan);
  std::string managerSocketDir = managerAddress.peer();
  int retVal = 0;
  struct sigaction sigaction;

  // (1) Set allowed uid and gid set
  try {
    m_permittedUIDSet = permittedUIDSet;
    m_permittedGIDSet = permittedGIDSet;
  } catch (std::bad_alloc &) {
    std::cerr << "Out of memory when copying permissions" << std::endl;
    goto COPY_PERMISSIONS_ERROR;
  }

  // (2) set buffer size
  m_defaultBufferSize = defaultBufferSize;

  // (3) Open the log file unbuffered
  if ("-" == logFilePath) {
    // use stdout
    streambuf = std::cout.rdbuf();
  } else {
    // set logfile unbuffered
    m_logFile.rdbuf()->pubsetbuf(0, 0);

    // std::cout << _getumask() << std::endl;

    if (0 != _getumask()) {
      umask(0); // zero out the umask so that directories are created correctly
    }

    // create log file directory
    if (0 != FileUtil::mkdirpForFile(logFilePath.c_str(), 0777)) {
      std::cerr << "Could not create directory for default log file \""
                << logFilePath << "\"" << std::endl;
      goto MKDIRP_ERROR;
    }

    if (FileUtil::exists(logFilePath.c_str())) {
      // Backup the previous file to ".last"
      std::string backup = logFilePath;
      backup += ".last";
      FileUtil::rename(logFilePath.c_str(), backup.c_str());
      FileUtil::remove(logFilePath.c_str());
    }

    // Create the file with the right permissions
    int fd = FileUtil::open(logFilePath.c_str(),
                            OpeningMode::CREATE_OR_TRUNCATE, 0666);
    if (fd < 0) {
      std::cerr << "Could not create the log file at \"" << logFilePath << "\""
                << std::endl;
      goto LOG_FILE_OPEN_ERROR;
    } else {
      close(fd);
    }

    // open the file
    m_logFile.open(logFilePath.c_str(), std::ios_base::out);
    if (!m_logFile.is_open()) {
      std::cerr << "Could not open the log file at \"" << logFilePath << "\""
                << std::endl;
      goto LOG_FILE_OPEN_ERROR;
    }

    // get the streambuf
    streambuf = m_logFile.rdbuf();
  }

  // create the output stream on the streambuf
  try {
    m_logStream = new std::ostream(streambuf);
  } catch (std::bad_alloc &) {
    std::cerr << "Out of memory when creating log stream" << std::endl;
    goto CREATE_LOG_STREAM_ERROR;
  }

  // (4) open listening UDS

  // try to make the directory
  if (managerSocketDir[managerSocketDir.size() - 1] != '/') {
    // Need to make sure there is a trailing '/' or otherwise
    // tdefu::FileUtil::mkdirpForFile would ignore the last part of the path
    managerSocketDir += '/';
  }

  FileUtil::mkdirpForFile(managerSocketDir.c_str(), 0777);

  if (0 != m_link.init(managerAddress,
                       ShMemBCastProtocol::UNIX_DOMAIN_SOCKET_TYPE)) {
    goto LINK_INIT_ERROR;
  }

  // tte::tteso::UnixSocketUtil::bind() changes mode to 0755, so reset again!
  ::chmod(managerSocketDir.c_str(), 0777);

  // ::system("/bin/ls -l /spare/local/.smb_manager/spaunov/");

  // (5) ignore SIGPIPE
  sigaction.sa_handler = SIG_IGN;
  ::sigemptyset(&(sigaction.sa_mask));
  sigaction.sa_flags = 0;
  if (0 != ::sigaction(SIGPIPE, &sigaction, 0)) {
    retVal = -EINTR;
    goto SIGACTION_ERROR;
  }

  // (6) Add to dispatcher
  if (DispatcherBase::ON_READ !=
      (retVal = m_dispatcher.addChannel(this, DispatcherBase::ON_READ))) {
    goto DISPATCHER_ERROR;
  }

  // log starting stats
  printLogPrefix();
  (*m_logStream) << std::endl;

  (*m_logStream)
      << "---------------------------------------------------------------"
      << std::endl;

  (*m_logStream) << "Manager Started with Settings:" << std::endl;
  (*m_logStream) << "  VLAN                : " << vlan << std::endl;

  (*m_logStream) << "  Permitted UID's     : ";
  if (!m_permittedUIDSet.empty()) {
    std::set<uid_t>::const_iterator iter = m_permittedUIDSet.begin();
    std::set<uid_t>::const_iterator iterEnd = m_permittedUIDSet.end();

    (*m_logStream) << *iter;
    iter++;

    for (; iterEnd != iter; iter++) {
      (*m_logStream) << "," << *iter;
    }
  }
  (*m_logStream) << std::endl;

  (*m_logStream) << "  Permitted GID's     : ";
  if (!m_permittedGIDSet.empty()) {
    std::set<gid_t>::const_iterator iter = m_permittedGIDSet.begin();
    std::set<gid_t>::const_iterator iterEnd = m_permittedGIDSet.end();

    (*m_logStream) << *iter;
    iter++;

    for (; iterEnd != iter; iter++) {
      (*m_logStream) << "," << *iter;
    }
  }
  (*m_logStream) << std::endl;

  (*m_logStream) << "  Default Buffer Size : " << m_defaultBufferSize
                 << std::endl;
  (*m_logStream) << "  Log File            : " << logFilePath << std::endl;

  (*m_logStream)
      << "---------------------------------------------------------------"
      << std::endl;

  // success!
  return 0;

DISPATCHER_ERROR:
SIGACTION_ERROR:
  m_link.close();

LINK_INIT_ERROR:
  delete m_logStream;
  m_logStream = 0;

CREATE_LOG_STREAM_ERROR:
LOG_FILE_OPEN_ERROR:
MKDIRP_ERROR:
COPY_PERMISSIONS_ERROR:
  return retVal;
}

int ShMemBCastManager::onRead(void) {
  int clientFd = -1;
  if (0 > (clientFd = m_link.accept())) {
    // silently drop this.
    return 0;
  }

  pid_t pid;
  uid_t uid;
  gid_t gid;
  if (0 != UnixSocketUtil::getCredentials(&pid, &uid, &gid, clientFd)) {
    printLogPrefix();
    (*m_logStream) << "Failed to get client credentials. Dropping connection"
                   << std::endl;
    return 0;
  }

  // get the passwd object for this client
  struct passwd *userDetails = ::getpwuid(uid);
  struct group *groupDetails = ::getgrgid(gid);

  if ((m_permittedUIDSet.end() == m_permittedUIDSet.find(uid)) &&
      (m_permittedGIDSet.end() == m_permittedGIDSet.find(gid))) {
    // cannot accept this client
    ::close(clientFd);

    // log rejection
    printLogPrefix();
    (*m_logStream) << "Rejected connection from Process " << pid
                   << ", running under user "
                   << ((0 == userDetails) ? ("<unknown>")
                                          : (userDetails->pw_name))
                   << " (" << uid << ") and group "
                   << ((0 == groupDetails) ? ("<unknown>")
                                           : (groupDetails->gr_name))
                   << " (" << gid << ")" << std::endl;

    return 0;
  }

  // log acceptance
  printLogPrefix();
  (*m_logStream) << "Accepted connection from Process " << pid
                 << ", running under user "
                 << ((0 == userDetails) ? ("<unknown>")
                                        : (userDetails->pw_name))
                 << " (" << uid << ") and group "
                 << ((0 == groupDetails) ? ("<unknown>")
                                         : (groupDetails->gr_name))
                 << " (" << gid << ")" << std::endl;

  // add client to dispatcher to handle subscription
  Client *client = new Client(clientFd, pid, this);
  m_dispatcher.addChannel(client, DispatcherBase::ON_READ);

  return 0;
}
