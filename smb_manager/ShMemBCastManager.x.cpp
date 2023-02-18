
#include "ShMemBCastManager.h"

#include <core/link/ShMemBCastProtocol.h>

#include <core/utils/DaemonUtil.h>
#include <core/utils/FileUtil.h>
#include <core/utils/StrToInt.h>

#include <boost/token_iterator.hpp>
#include <boost/tokenizer.hpp>

#include <iostream>
#include <set>
#include <string>

#include <core/utils/ConfigFileParser.h>
#include <getopt.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
const char *const DEFAULT_LOG_FILE_SUFFIX = "/smb_manager.log";
const size_t LOG_FILE_PATH_LENGTH = 256;
const size_t MINIMUM_BUFFER_SIZE = USHRT_MAX;
const size_t DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024;

/**
 * Generates the log file path
 *
 * @param path location to place log file path
 * @param size size of the path
 *
 * @return 0 on success, non-zero on error
 */
int generateDefaultLogFilePath(char *path, size_t size, std::string vlan) {
  // initialize path
  path[size - 1] = '\0';
  size_t bytesWritten =
      ::snprintf(path, size, ShMemBCastProtocol::MANAGER_VLAN_LOCATION_FORMAT,
                 vlan.c_str());
  if (('\0' != path[size - 1]) ||
      ((size - bytesWritten) < (::strlen(DEFAULT_LOG_FILE_SUFFIX) + 1))) {
    // too long
    return -1;
  }
  ::strcat(path, DEFAULT_LOG_FILE_SUFFIX);

  return 0;
}

/**
 * Parses a User value
 *
 * @param permittedUIDSet The Set to add the UID value to
 * @param value The user permission value to parse
 *
 * @return 0 on success, non-zero on error. Prints error messages to stderr
 */
int parseUserValue(std::set<uid_t> *permittedUIDSet, const std::string &value) {
  // check if it is a uid
  const char *parseEnd;
  uid_t uid = StrToInt::parseUint(value.c_str(), StrToInt::npos, 10, &parseEnd);
  if (parseEnd == (value.c_str() + value.size())) {
    // is a uid. Add and return
    permittedUIDSet->insert(uid);
    return 0;
  }

  // At this point, value is not a uid, but a username. Parse into a uid to add
  // to the set
  struct passwd *userDetails = ::getpwnam(value.c_str());
  if (0 == userDetails) {
    std::cerr << "Could not find User Details for username \"" << value << "\""
              << std::endl;
    return -1;
  }

  // add the uid to the set and return
  permittedUIDSet->insert(userDetails->pw_uid);
  return 0;
}

/**
 * Parses a Group value
 *
 * @param permittedUIDSet The Set to add the UID value to
 * @param value The group permission value to parse
 *
 * @return 0 on success, non-zero on error. Prints error messages to stderr
 */
int parseGroupValue(std::set<gid_t> *permittedGIDSet,
                    const std::string &value) {
  // check if it is a gid
  const char *parseEnd;
  gid_t gid = StrToInt::parseUint(value.c_str(), StrToInt::npos, 10, &parseEnd);
  if (parseEnd == (value.c_str() + value.size())) {
    // is a gid. Add and return
    permittedGIDSet->insert(gid);
    return 0;
  }

  // At this point, value is not a gid, but a groupname. Parse into a gid to add
  // to the set
  struct group *groupDetails = ::getgrnam(value.c_str());
  if (0 == groupDetails) {
    std::cerr << "Could not find Group Details for groupname \"" << value
              << "\"" << std::endl;
    return -1;
  }

  // add the gid to the set and return
  permittedGIDSet->insert(groupDetails->gr_gid);
  return 0;
}

/**
 * Parses the permissions argument to generate the permission sets
 *
 * @param permittedUIDSet The UID Set to populate
 * @param permittedGIDSet The GID Set to populate
 * @param permissions The permissions argument string
 *
 * @return 0 on success, non-zero on error. Errors are logged to stderr
 */
int parsePermissions(std::set<uid_t> *permittedUIDSet,
                     std::set<gid_t> *permittedGIDSet,
                     const std::string &permissions) {
  typedef boost::token_iterator_generator<boost::char_separator<char>>::type
      TokenIterator;

  // tokenize the permissions
  boost::char_separator<char> separator(",");
  TokenIterator iter = boost::make_token_iterator<std::string>(
      permissions.begin(), permissions.end(), separator);
  TokenIterator iterEnd = boost::make_token_iterator<std::string>(
      permissions.end(), permissions.end(), separator);

  // iterate through
  for (; iterEnd != iter; iter++) {
    size_t colonIndex = (*iter).find_first_of(':');
    if ((std::string::npos == colonIndex) ||
        ((colonIndex + 1) == (*iter).size())) {
      // uh oh!
      std::cerr << "Permission Entry \"" << *iter << "\" is invalid"
                << std::endl;
      return -1;
    }

    const std::string &permissionType = (*iter).substr(0, colonIndex);
    const std::string &permissionValue = (*iter).substr(colonIndex + 1);

    if ("u" == permissionType) {
      if (0 != parseUserValue(permittedUIDSet, permissionValue)) {
        return -1;
      }
    } else if ("g" == permissionType) {
      if (0 != parseGroupValue(permittedGIDSet, permissionValue)) {
        return -1;
      }
    } else {
      // uh oh!
      std::cerr << "Permission Type \"" << permissionType
                << "\" in Permission Entry \"" << *iter << "\" is invalid"
                << std::endl;
      return -1;
    }
  }

  // success!
  return 0;
}

void usage(const char *programName) {
  std::cerr << programName << " [option]*" << std::endl

            << "Option Descriptions:" << std::endl

            << "  --vlan        | -v <string>  : Specify vlan to manage. "
            << "(default: ${USER})" << std::endl

            << "  --permissions | -p <list>    : Access permissions for this "
            << "vlan." << std::endl
            << "    ABNF:" << std::endl
            << "    perms     = *((user / group),)(user / group)" << std::endl
            << "    user      = username / uid" << std::endl
            << "    username  = \"u:\" *(ALPHA / DIGIT)" << std::endl
            << "    uid       = \"u:\" *DIGIT" << std::endl
            << "    group     = groupname / gid" << std::endl
            << "    groupname = \"g:\" *(ALPHA / DIGIT)" << std::endl
            << "    gid       = \"g:\" *DIGIT" << std::endl
            << "    (default: u:${USER})"
            << "    Only client processes run with EITHER one of the allowed "
            << "    usernames OR one of the allowed groupnames will be accepted"
            << std::endl

            << "  --buffer_size | -b <integer> : default buffer size in bytes "
            << "(default: 4MiB, constraints: > 64KiB)" << std::endl

            << "  --log_file    | -l <string>  : path to log file. Use \"-\" "
            << "for stdout. Any required parent directories are created "
            << "(default: /spare/local/smb_manager/${VLAN}/smb_manager.log)"
            << std::endl

            << "  --daemon      | -d           : daemonize process (REQUIRES "
            << "--log_file)" << std::endl

            << "  --help        | -[h?]        : display this help message"
            << std::endl;
}
} // namespace

int main(int argc, char **argv) {
  // option variables
  std::string vlan;
  std::string permissions;
  uint64_t bufferSize = 0;
  std::string logFilePath;
  bool daemon = false;

  // setup getopt_long options
  const char *optstring = "-:v:p:b:l:dh?";
  const struct option longopts[] =
      // {char* name, int has_arg, int* flag, int val}
      {{"vlan", required_argument, 0, 'v'},
       {"permissions", required_argument, 0, 'p'},
       {"buffer_size", required_argument, 0, 'b'},
       {"log_file", required_argument, 0, 'l'},
       {"daemon", no_argument, 0, 'd'},
       {"help", no_argument, 0, 'h'},
       {0, 0, 0, 0}};

  // get args
  ::opterr = 0;
  int option;
  while (-1 != (option = ::getopt_long(argc, argv, optstring, longopts, 0))) {
    switch (option) {
    case 'v': {
      vlan = ::optarg;
    } break;

    case 'p': {
      permissions = ::optarg;
    } break;

    case 'b': {
      bufferSize = StrToInt::parseUint(::optarg);
      if (0 == bufferSize) {
        std::cerr << "Please enter a valid buffer size in bytes" << std::endl;
        usage(argv[0]);
        return 1;
      }
    } break;

    case 'l': {
      logFilePath = ::optarg;
    } break;

    case 'd': {
      daemon = true;
    } break;

    case 'h':
    case '?': {
      usage(argv[0]);
      return 1;
    } break;

    case ':': {
      // missing argument
      switch (optopt) {
      case 'v': {
        std::cerr << "Please specify the vlan to use" << std::endl;
        usage(argv[0]);
        return 1;
      } break;

      case 'p': {
        std::cerr << "Please specify access permissions" << std::endl;
        usage(argv[0]);
      } break;

      case 'b': {
        std::cerr << "Please enter a valid buffer size in bytes" << std::endl;
        usage(argv[0]);
        return 1;
      } break;

      case 'l': {
        std::cerr << "Please specify a log file" << std::endl;
        usage(argv[0]);
        return 1;
      } break;
      }
    } break;
    }
  }

  // verify arguments
  if (daemon && ("-" == logFilePath)) {
    // daemon and stdout log file?
    std::cerr << "Log File cannot be stdout when run as a daemon" << std::endl;
    usage(argv[0]);
    return 1;
  }

  if (vlan.empty()) {
    // set default vlan if unspecified
    struct passwd *userDetails = ::getpwuid(::getuid());

    vlan = userDetails->pw_name;
  }

  if (permissions.empty()) {
    // set default vlan if unspecified
    struct passwd *userDetails = ::getpwuid(::getuid());

    permissions = std::string("u:") + userDetails->pw_name;
  }

  if (0 == bufferSize) {
    // set default buffer size if unset
    bufferSize = DEFAULT_BUFFER_SIZE;
  }
  if (bufferSize < MINIMUM_BUFFER_SIZE) {
    std::cerr << "Default buffer Size must be at least " << MINIMUM_BUFFER_SIZE
              << " bytes to fit at least one datagram!" << std::endl;
    return 1;
  }

  if (logFilePath.empty()) {
    // use default log file
    // generate log file path
    char path[LOG_FILE_PATH_LENGTH];
    if (0 != generateDefaultLogFilePath(path, sizeof(path), vlan)) {
      std::cerr << "Could not generate default log file path" << std::endl;
      usage(argv[0]);
      return 1;
    }
    logFilePath = path;
  }

  std::set<uid_t> permittedUIDSet;
  std::set<gid_t> permittedGIDSet;
  if (0 != parsePermissions(&permittedUIDSet, &permittedGIDSet, permissions)) {
    return 1;
  }

  umask(0); // zero out umask otherwise created files and directories
            // will not be writable by group and other

  // create manager
  ShMemBCastManager manager;
  if (0 != manager.init(vlan, permittedUIDSet, permittedGIDSet, bufferSize,
                        logFilePath)) {
    std::cerr << "Could not initialize Manager" << std::endl;
    return 1;
  }

  // daemonize
  if (daemon) {
    if (0 != DaemonUtil::daemonize()) {
      std::cerr << "Could not daemonize" << std::endl;
      return 1;
    }
  }

  manager.run();

  // This is a daemon. It should never exit
  return 255;
}
