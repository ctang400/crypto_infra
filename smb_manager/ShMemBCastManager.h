#ifndef DAEMONS_SHMEMBCASTMANAGER_H_
#define DAEMONS_SHMEMBCASTMANAGER_H_

/***
 * @file ShMemBCastManager.h
 *
 * @brief
 * This class provides the bulk of the implementation for the
 * ShMemBCast Manager daemon, which creates and manages the shared
 * memory used for ShMemBCast. This also facilitates a UDP-like
 * connectionless paradigm.
 *
 * @description
 * This class encapsulates the ShMemBCast Manager. The manager's
 * function is to match up readers and writers with the appropriate
 * buffers. This provides two advantages over the previous
 * implementations:
 * -# Writers no longer have to maintain the listening Unix Domain
 *    Socket for incoming reader connections. This makes it more UDP
 *    like, since the writer simply "connects" and starts writing. The
 *    manager takes over the job of connecting readers to the appropriate
 *    channel.
 * -# If a writer is restarted, but the readers are still join()'d to
 *    the channel, the writer does not end up creating a new channel that
 *    forces readers to reconnect. The manager will connect the writer to
 *    the existing channel, and the writer will pick up where he left
 *    off.
 *
 * @warning
 * If the manager crashes or is closed, any restarted manager is
 * unaware of any existing channels. While no channel will be
 * destroyed, and the subscribers can still communicate, no new
 * subscribers can be added. Connecting to a channel with the same
 * name as an existing channel creates a brand new channel.
 */

#include <core/dispatcher/ChannelBase.h>
#include <core/dispatcher/DispatcherBase.h>
#include <core/dispatcher/SelectDispatcher.h>
#include <core/link/IPCAddress.h>
#include <core/link/ShMemBCastProtocol.h>
#include <core/link/UnixLink.h>
#include <core/link/UnixServerLink.h>

#include <core/link/UnixSocketUtil.h>
#include <core/utils/FileUtil.h>

#include <fstream>
#include <ios>
#include <iostream>
#include <list>
#include <memory>
#include <ostream>
#include <set>
#include <string>

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

class ShMemBCastManager : public ReadCB {
private:
  /***
   * typedef ShMemBCastProtocol to keep from having absurdly long
   * identifiers.
   */
  typedef ShMemBCastProtocol Protocol;

  /***
   * Class that holds and monitors the client links. It must be
   * created with new as it does indicate TTECH_DELETE_CHAN to the
   * dispatcher.
   */
  class Client : public ReadCB {
  private:
    /***
     * Information about a subscription
     *
     * @var Subscription::m_channelName The name of the channel
     * subscribed to. Not owned by the struct
     * @var Subscription::m_writer Whether or not this subscription is
     * a writer
     */
    struct Subscription {
      const char *m_channelName;
      bool m_writer;
    };

    /***
     * Sends an approval or denial message
     *
     * @param approval set to true to send an approval message, false
     * to send a denial message
     *
     * @return 0 on success, non-zero on error
     */
    int sendApprovalDenialMessage(bool approval);

    /***
     * Handles a Event Mode Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::EventModeRequest *request);

    /***
     * Handles a No Event Mode Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::NoEventModeRequest *request);

    /***
     * Handles a Writer Subscribe Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::WriterSubscribeRequest *request);

    /***
     * Handles a Reader Subscribe Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::ReaderSubscribeRequest *request);

    /***
     * Handles a Writer Unsubscribe Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::WriterUnsubscribeRequest *request);

    /***
     * Handles a Reader Unsubscribe Request
     *
     * @param request The request to handle
     *
     * @return 0 on success, non-zero on fatal error
     */
    int handleMessage(Protocol::ReaderUnsubscribeRequest *request);

    /***
     * Disconnects this client, unsubscribing from all subscriptions
     */
    void disconnect(void);

    static const struct timeval TIMEOUT;

    UnixLink m_link;
    std::list<Subscription> m_subscriptions;
    ShMemBCastManager *const m_manager;
    const pid_t m_pid;
    bool m_eventMode;

  public:
    /***
     * Creates the client using the specified file descriptor.
     *
     * @param fd The file descriptor to map this link to
     * @param pid The pid of this client, for logging purposes
     * @param manager The manager that this client is associated
     * with.
     */
    Client(int fd, const pid_t pid, ShMemBCastManager *manager);

    /***
     * sends a channel subscription event to the client, if it is an
     * Event Mode connection
     *
     * @param numReaders number of readers
     * @param channelName name of channel
     */
    void sendChannelSubscriptionEvent(uint16_t numReaders, const char *channel);

    // ReadCB Functions
    int onRead(void);

    int readFileDescriptor(void) const;

    // ChannelBase Functions
    int onClose(void);

    int mode(void) const;
  };

  /***
   * This struct holds the channel data
   *
   * @var Channel::m_readers List of reader clients. Clients can be
   * repeated. The pointers are not owned by the Channel
   * @var Channel::m_writer The client which has the writer
   * subscription
   * @var Channel::m_name The name of the channel
   * @var Channel::m_fd The file descriptor of the datagram board
   * associated with this channel
   */
  struct Channel {
    std::list<Client *> m_readers;
    Client *m_writer;
    const char *m_name;
    int m_fd;
  };

  /***
   * subscribe to the requested channel, and create the channel
   * if non-existent.
   *
   * @param client The client which owns this subscription
   * @param channelName A NUL-terminated channel name string.
   * @param writer true if this subscriber intends to be a writer
   * @param requestedSize Requested Buffer size if this is a new channel
   *
   * @return the Channel on success, 0 on error
   */
  Channel *subscribe(Client *client, const char *channelName, bool writer,
                     uint32_t requestedSize);

  /***
   * unsubscribe from the requested channel, and delete the
   * channel if it has no subscribers.
   *
   * @note This function removes and cleans up the channel if it has
   * no subscribers after the unsubscription. <strong>All members are
   * delete'd.</strong> As such, make sure not to reference any
   * members owned by the Channel struct after the unsubscribe() call.
   *
   * @param client The client that owns the subscription
   * @param channelName a NUL-terminated channel name string
   * @param writer Whether the unsubscriber was a writer
   *
   * @return 0 on success, non-zero on error
   */
  int unsubscribe(Client *client, const char *channelName, bool writer);

  /***
   * Prints the prefix for a log message
   */
  void printLogPrefix(void);

  static const size_t LOG_TIME_LENGTH = 20;
  static const char *const LOG_TIME_FORMAT;
  static const char *const LOG_TIME_ERROR;

  UnixServerLink m_link;
  SelectDispatcher m_dispatcher;
  std::list<Channel> m_channels;
  std::set<uid_t> m_permittedUIDSet;
  std::set<gid_t> m_permittedGIDSet;
  std::ofstream m_logFile;
  uint64_t m_defaultBufferSize;
  std::ostream *m_logStream;

public:
  /***
   * Create an instance of ShMemBCastManager which creates buffers of
   * 'bufferSize' bytes. Please be sure to call init() to complete
   * initialization.
   */
  ShMemBCastManager(void);

  /***
   * Destructor
   */
  ~ShMemBCastManager(void);

  /***
   * Initializes the Manager
   *
   * 1) Copies the Permitted UID and GID sets
   * 2) Sets the default buffer size to 'defaultBufferSize'
   * 3) Open the log file
   * 4) Opens the listening UDS at
   * ShMemBCastProtocol::getManagerIPCAddress()
   * 5) Sets SIGPIPE to ignore
   * 6) Adds itself to the dispatcher.
   *
   * @param vlan The vlan to manager
   * @param permittedUIDSet The set of permitted UID's
   * @param permittedGIDSet The set of permitted GID's
   * @param defaultBufferSize The minimum buffer size in bytes
   * @param logFilePath Path to the log file to open. The special value of "-"
   * indicates stdout
   *
   * @return 0 on success, non-zero on error
   */
  int init(const std::string &vlan, const std::set<uid_t> &permittedUIDSet,
           const std::set<gid_t> &permittedGIDSet, uint64_t defaultBufferSize,
           const std::string &logFilePath);

  /***
   * Runs the Manager. Call init() before calling this function. This
   * function should not return.
   */
  void run(void);

  // ReadCB Functions
  int onRead(void);

  int readFileDescriptor(void) const;

  // ChannelBase Functions
  int onClose(void);

  int mode(void) const;
};

// inline and template functions
inline ShMemBCastManager::Client::Client(int fd, pid_t pid,
                                         ShMemBCastManager *manager)
    : m_link(fd), m_subscriptions(), m_manager(manager), m_pid(pid),
      m_eventMode(false) {}

inline int ShMemBCastManager::Client::readFileDescriptor(void) const {
  return m_link.fileDescriptor();
}

inline int ShMemBCastManager::Client::onClose(void) {
  return TTECH_DELETE_CHAN;
}

inline int ShMemBCastManager::Client::mode(void) const {
  return ChannelBase::SELECT_MODE;
}

inline void ShMemBCastManager::printLogPrefix(void) {
  // static buffer to remove allocation time
  static char timeString[LOG_TIME_LENGTH];

  // get time
  time_t rawTime = ::time(0);
  struct tm *localTime = ::localtime(&rawTime);
  if (0 ==
      ::strftime(timeString, sizeof(timeString), LOG_TIME_FORMAT, localTime)) {
    ::strncpy(timeString, LOG_TIME_ERROR, LOG_TIME_LENGTH - 1);
    timeString[LOG_TIME_LENGTH - 1] = '\0';
  }

  // skip the std::endl as this is the beginning of a line
  (*m_logStream) << timeString << ": ";
}

inline ShMemBCastManager::ShMemBCastManager(void)
    : m_link(), m_dispatcher(), m_channels(), m_permittedUIDSet(),
      m_permittedGIDSet(), m_logFile(), m_defaultBufferSize(0), m_logStream(0) {
}

inline ShMemBCastManager::~ShMemBCastManager(void) { delete m_logStream; }

inline void ShMemBCastManager::run(void) { m_dispatcher.run(); }

inline int ShMemBCastManager::readFileDescriptor(void) const {
  return m_link.fileDescriptor();
}

inline int ShMemBCastManager::onClose(void) {
  // log
  printLogPrefix();
  (*m_logStream) << "Manager Ending" << std::endl;

  return 0;
}

inline int ShMemBCastManager::mode(void) const {
  return ChannelBase::SELECT_MODE;
}

#endif // DAEMONS_SHMEMBCASTMANAGER_H_
