#include "Throughput.h"
#include "includes.h"
#include "defines.h"

char coresList[101];  // buffer for preparing the list of lcores for DPDK init (like a command line argument)
char numChannels[11]; // buffer for printing the number of memory channels into a string for DPDK init (like a command line argument)

Throughput::Throughput(){
  // initialize some data members to default or invalid value //Just in case of not setting them in the configuration file and the Tester did not exit
  forward = 1;                   // default value, forward direction is active
  reverse = 1;                   // default value, reverse direction is active
  promisc = 0;                   // default value, promiscuous mode is inactive
  cpu_left_sender = -1;          // MUST be set in the config file if forward != 0
  cpu_right_receiver = -1;       // MUST be set in the config file if forward != 0
  cpu_right_sender = -1;         // MUST be set in the config file if reverse != 0
  cpu_left_receiver = -1;        // MUST be set in the config file if reverse != 0
  memory_channels = 1;           // default value, this value will be set, if not specified in the config file
  fwd_dport_min = 1;             // default value: as recommended by RFC 4814
  fwd_dport_max = 49151;         // default value: as recommended by RFC 4814
  fwd_sport_min = 1024;          // default value: as recommended by RFC 4814
  fwd_sport_max = 65535;         // default value: as recommended by RFC 4814
  rev_dport_min = 1;             // default value: as recommended by RFC 4814
  rev_dport_max = 49151;         // default value: as recommended by RFC 4814
  rev_sport_min = 1024;          // default value: as recommended by RFC 4814
  rev_sport_max = 65535;         // default value: as recommended by RFC 4814
  system_ports = 1;
};

// reports the TSC of the core (in the variable pointed by the input parameter), on which it is running
int report_tsc(void *par)
{
  *(uint64_t *)par = rte_rdtsc();
  return 0;
}

// checks if the TSC of the given lcore is synchronized with that of the main core
// Note that TSCs of different pysical CPUs may be different, which would prevent siitperf from working correctly!
void check_tsc(int cpu, const char *cpu_name) {
  uint64_t tsc_before, tsc_reported, tsc_after;

  tsc_before = rte_rdtsc();
  if ( rte_eal_remote_launch(report_tsc, &tsc_reported, cpu) ){
    rte_exit(EXIT_FAILURE, "Error: could not start TSC checker on core #%i for %s!\n", cpu, cpu_name);
    //std::cerr << "Error: could not start TSC checker on core " << cpu << " for " << cpu_name << "!" << std::endl;
    //return -1;
  }
  rte_eal_wait_lcore(cpu);
  tsc_after = rte_rdtsc();
  if ( tsc_reported < tsc_before || tsc_reported > tsc_after ){
    rte_exit(EXIT_FAILURE, "Error: TSC of core #%i for %s is not synchronized with that of the main core!\n", cpu, cpu_name);
    //std::cerr << "Error: TSC of core " << cpu << " for " << cpu_name << "is not synchronized with that of the main core!" << std::endl;
    //return -1;
  };
}

// finds a 'key' (name of a parameter) in the 'line' string
// '#' means comment, leading spaces and tabs are skipped
// return: the starting position of the key, if found; -1 otherwise
int Throughput::findKey(const char *line, const char *key) {
  int line_len, key_len; // the lenght of the line and of the key
  int pos; // current position in the line

  line_len=strlen(line);
  key_len=strlen(key);
  for ( pos=0; pos<line_len-key_len; pos++ ) {
    if ( line[pos] == '#' ) // comment
      return -1;
    if (line[pos] == '[')  //new lwB4
      return -2;
    if ( line[pos] == ' ' || line[pos] == '\t' )
      continue;
    if ( strncmp(line+pos,key,key_len) == 0 )
      return pos+strlen(key);
  }
  return -1;
}

// skips leading spaces and tabs, and cuts off tail starting by a space, tab or new line character
// it is needed, because inet_pton cannot read if there is e.g. a trailing '\n'
// WARNING: the input buffer is changed!
char *prune(char *s) {
  int len, i;
 
  // skip leading spaces and tabs 
  while ( *s==' ' || *s=='\t' )
    s++;

  // trim string, if space, tab or new line occurs
  len=strlen(s);
  for ( i=0; i<len; i++ )
    if ( s[i]==' ' || s[i]=='\t' || s[i]=='\n' ) {
      s[i]=(char)0;
      break;
    }
  return s;
}

// checks if there is some non comment information in the line
int nonComment(const char *line) {
  int i;

  for ( i=0; i<LINELEN; i++ ) {
    if ( line[i]=='#' || line[i]=='\n' )
      return 0; // line is comment or empty 
    else if ( line[i]==' ' || line[i]=='\t' )
      continue; // skip space or tab, see next char
    else
      return 1; // there is some other character
  }
  // below code should be unreachable
  return 1;
}

int Throughput::readConfigFile(const char *filename) {
  FILE *f; 	// file descriptor
  char line[LINELEN+1]; // buffer for reading a line of the input file
  int pos; 	// position in the line after the key (parameter name) was found
  uint8_t *m; 	// pointer to the MAC address being read
  int line_no;	// line number for error message

  f=fopen(filename,"r");
  if ( f == NULL ) {
    std::cerr << "Input Error: Can't open file '" << filename << "'." << std::endl;
    return -1;
  }
  for ( line_no=1; fgets(line, LINELEN+1, f); line_no++ ) {
    if ( (pos = findKey(line, "IPv6-L-BG")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_left_bg)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-L-BG' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-R-BG")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_right_bg)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-R-BG' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-server")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&ipv4_server)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv4-server' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-L-Tester")) >= 0 ) {
      m=mac_left_tester;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-L-Tester' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "MAC-R-Tester")) >= 0 ) {
      m=mac_right_tester;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-R-Tester' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-L-DUT")) >= 0 ) {
      m=mac_left_dut;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-L-DUT' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-R-DUT")) >= 0 ) {
      m=mac_right_dut;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-R-DUT' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Forward")) >= 0 ) {
      sscanf(line+pos, "%d", &forward);
      if (!(forward == 0 || forward == 1))
      {
        std::cerr << "Input Error: 'Forward' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Reverse")) >= 0 ) {
      sscanf(line+pos, "%d", &reverse);
      if (!(reverse == 0 || reverse == 1))
      {
        std::cerr << "Input Error: 'Reverse' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Promisc")) >= 0 ) {
      sscanf(line+pos, "%d", &promisc);
      if (!(promisc == 0 || promisc == 1))
      {
        std::cerr << "Input Error: 'Promisc' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-L-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_left_sender);
      if ( cpu_left_sender < 0 || cpu_left_sender >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-L-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-R-Recv")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_right_receiver);
      if ( cpu_right_receiver < 0 || cpu_right_receiver >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-R-Recv' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-R-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_right_sender);
      if ( cpu_right_sender < 0 || cpu_right_sender >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-R-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-L-Recv")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_left_receiver);
      if ( cpu_left_receiver < 0 || cpu_left_receiver >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-L-Recv' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MEM-Channels")) >= 0 ) {
      sscanf(line+pos, "%hhu", &memory_channels);
      if ( memory_channels <= 0 ) {
        std::cerr << "Input Error: 'MEM-Channels' must be > 0." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &fwd_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &fwd_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-port-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'FG-Rev-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &fwd_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &fwd_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-sport-max'." << std::endl;
        return -1;
      }
    
    } else if ( (pos = findKey(line, "Num-lwB4s")) >= 0 ){
      sscanf(line + pos, "%u", &number_of_lwB4s);
      if ( number_of_lwB4s < 1 || number_of_lwB4s > 1000000 )
      {
        std::cerr << "Input Error: 'Num-lwB4s' must be >= 1 and <= 1000000." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Select-lwB4") ) >= 0){
      sscanf(line + pos, "%u", &select_lwB4);
      if ( select_lwB4 > 3 )
      {
        std::cerr << "Input Error: 'Select-lwB4' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-tunnel")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_tunnel)) != 1 ) {
         std::cerr << "Input Error: Bad 'IPv6-tunnel' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "System-ports")) >= 0 ) {
      sscanf(line+pos, "%d", &system_ports);
      if (!(system_ports == 0 || system_ports == 1))
      {
        std::cerr << "Input Error: 'System-ports' must be either 1 for allowing the use of system ports or 0 for not allowing it." << std::endl;
        return -1;
      }
    } else if ( nonComment(line) ) { // It may be too strict!
        std::cerr << "Input Error: Cannot interpret '" << filename << "' line " << line_no << ":" << std::endl;
        std::cerr << line << std::endl;
        return -1;
    } 
  }
  fclose(f);
  
  // check if at least one direction is active (compulsory for stateless tests)
  if ( forward == 0 && reverse == 0 ) {
    std::cerr << "Input Error: No active direction was specified." << std::endl;
    return -1;
  } 
  
  // check if the necessary lcores were specified
  if ( forward ) {
    if ( cpu_left_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_right_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Recv' was specified." << std::endl;
      return -1;
    }
  }
  if ( reverse ) {
    if ( cpu_right_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_left_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Recv' was specified." << std::endl;
      return -1;
    }
  }
  return 0;
}

// reads the config files for lwB4s, creates lwB4_data object for each lwB4 and stores them in Array/Vector
int Throughput::readlwB4Data(const char *filename) {
  FILE *f; 	// file descriptor
  char line[LINELEN+1]; // buffer for reading a line of the input file
  int pos; 	// position in the line after the key (parameter name) was found
  uint8_t *m; 	// pointer to the MAC address being read
  int line_no;	// line number for error message

  lwB4_data tmp_obj;
  bool new_lwB4 = false;

  f=fopen(LWB4DATAFILE,"r");
  if ( f == NULL ) {
    std::cerr << "Input Error: Can't open file '" << LWB4DATAFILE << "'." << std::endl;
    return -1;
  }
  
  for ( line_no=1; fgets(line, LINELEN+1, f); line_no++ ) {
	  if ( (pos = findKey(line, "[]")) == -2 ){
		  if (new_lwB4){
			   tmp_lwb4data.push_back(tmp_obj);	
		  }
		  tmp_obj = {};
		  new_lwB4 = true;
    }
    else {
      if ( (pos = findKey(line, "b4-ipv6")) >= 0 ) {
        if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.b4_ipv6_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'b4-ipv6' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "br-address")) >= 0 ) {
        if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.aftr_tunnel_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'br-address' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "ipv4")) >= 0 ) {
        if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.ipv4_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'ipv4' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "psid-length")) >= 0 ) {
        sscanf(line+pos, "%u", &tmp_obj.psid_length);
        if ( tmp_obj.psid_length < 1 || tmp_obj.psid_length > 16 ) {
          std::cerr << "Input Error: 'psid_length' must be >= 1 and <= 16." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "psid")) >= 0 ) {
        sscanf(line+pos, "%u", &tmp_obj.psid);
        if ( tmp_obj.psid >= pow(2.0, tmp_obj.psid_length) ) {
          std::cerr << "ERROR: PSID cannot be greater then 2**psid_length" << std::endl;
          return -1;
        }
      }
	  }
  }   
  
  //Save the last lwB4 to the vector
  if (new_lwB4){
    tmp_lwb4data.push_back(tmp_obj);	
  }
  std::cout << "Number of lwB4s read from lwB4Data.conf: " << tmp_lwb4data.size() << std::endl;
  if (tmp_lwb4data.size() != number_of_lwB4s) {
    std::cerr << "Number of lwB4 number is not the same as declared in lw4o6.conf" << std::endl;
    return -1;
  }

  return 0;
}

// reads the command line arguments and stores the information in data members of class Throughput
// It may be called only AFTER the execution of readConfigFile
int Throughput::readCmdLine(int argc, const char *argv[])
{
  std::cout << "READ CMD STARTED" << std::endl;
  if (argc < 7)
  {
    printf("argc : %d\n", argc);
    std::cerr << "Input Error: Too few command line arguments." << std::endl;
    return -1;
  }
  if (sscanf(argv[1], "%hu", &ipv6_frame_size) != 1 || ipv6_frame_size < 84 || ipv6_frame_size > 1538)
  {
    std::cerr << "Input Error: IPv6 frame size must be between 84 and 1538." << std::endl;
    return -1;
  }
  // Further checking of the frame size will be done, when n and m are read.
  ipv4_frame_size = ipv6_frame_size - 20;
  if (sscanf(argv[2], "%u", &frame_rate) != 1 || frame_rate < 1 || frame_rate > 14880952)
  {
    // 14,880,952 is the maximum frame rate for 10Gbps Ethernet using 64-byte frame size
    std::cerr << "Input Error: Frame rate must be between 1 and 14880952." << std::endl;
    return -1;
  }
  if (sscanf(argv[3], "%hu", &test_duration) != 1 || test_duration < 1 || test_duration > 3600)
  {
    std::cerr << "Input Error: Test duration must be between 1 and 3600." << std::endl;
    return -1;
  }
  if (sscanf(argv[4], "%hu", &stream_timeout) != 1 || stream_timeout > 60000)
  {
    std::cerr << "Input Error: Stream timeout must be between 0 and 60000." << std::endl;
    return -1;
  }
  if (sscanf(argv[5], "%u", &n) != 1 || n < 2)
  {
    std::cerr << "Input Error: The value of 'n' must be at least 2." << std::endl;
    return -1;
  }
  if (sscanf(argv[6], "%u", &m) != 1)
  {
    std::cerr << "Input Error: Cannot read the value of 'm'." << std::endl;
    return -1;
  }
  std::cout << "READ CMD ENDED" << std::endl;
  return 0;
}

int Throughput::init(const char *argv0, uint16_t leftport, uint16_t rightport)
{
  const char *rte_argv[6];                                                     // parameters for DPDK EAL init, e.g.: {NULL, "-l", "4,5,6,7", "-n", "2", NULL};
  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1; // argc value for DPDK EAL init
  struct rte_eth_conf cfg_port;                                                // for configuring the Ethernet ports
  struct rte_eth_link link_info;                                               // for retrieving link info by rte_eth_link_get()
  int trials;                                                                  // cycle variable for port state checking

  // prepare 'command line' arguments for rte_eal_init
  rte_argv[0] = argv0; // program name
  rte_argv[1] = "-l";  // list of lcores will follow
  // Only lcores for the active directions are to be included (at least one of them MUST be non-zero)
  if (forward && reverse)
  {
    // both directions are active
    snprintf(coresList, 101, "0,%d,%d,%d,%d", cpu_left_sender, cpu_right_receiver, cpu_right_sender, cpu_left_receiver);
  }
  else if (forward)
    snprintf(coresList, 101, "0,%d,%d", cpu_left_sender, cpu_right_receiver); // only forward (left to right) is active
  else
    snprintf(coresList, 101, "0,%d,%d", cpu_right_sender, cpu_left_receiver); // only reverse (right to left) is active

  rte_argv[2] = coresList;
  std::cout << coresList << std::endl;
  rte_argv[3] = "-n";
  snprintf(numChannels, 11, "%hhu", memory_channels);
  rte_argv[4] = numChannels;
  rte_argv[5] = 0;

  std::cout << coresList << std::endl;
  if (rte_eal_init(rte_argc, const_cast<char **>(rte_argv)) < 0)
  {
    std::cerr << "Error: DPDK RTE initialization failed, Tester exits." << std::endl;
    return -1;
  }

  if (!rte_eth_dev_is_valid_port(leftport))
  {
    std::cerr << "Error: Network port #" << leftport << " provided as Left Port is not available, Tester exits." << std::endl;
    return -1;
  }
 
  if (!rte_eth_dev_is_valid_port(rightport))
  {
    std::cerr << "Error: Network port #" << rightport << " provided as Right Port is not available, Tester exits." << std::endl;
    return -1;
  }

  // prepare for configuring the Ethernet ports
  memset(&cfg_port, 0, sizeof(cfg_port));   // e.g. no CRC generation offloading, etc. (May be improved later!)
  cfg_port.txmode.mq_mode = RTE_ETH_MQ_TX_NONE; // no multi queues
  cfg_port.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE; // no multi queues

  if (rte_eth_dev_configure(leftport, 1, 1, &cfg_port) < 0)
  {
    std::cerr << "Error: Cannot configure network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }

  if (rte_eth_dev_configure(rightport, 1, 1, &cfg_port) < 0)
  {
    std::cerr << "Error: Cannot configure network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  // Important remark: with no regard whether actual test will be performed in the forward or reverese direction,
  // all TX and RX queues MUST be set up properly, otherwise rte_eth_dev_start() will cause segmentation fault.
  // Sender pool size calculation uses 0 instead of num_{left,right}_nets, when no actual frame sending is needed.

  // calculate packet pool sizes and then create the pools
  int left_sender_pool_size = senderPoolSize();
  int right_sender_pool_size = senderPoolSize();
  int receiver_pool_size = PORT_RX_QUEUE_SIZE + 2 * MAX_PKT_BURST + 100; // While one of them is processed, the other one is being filled.

  pkt_pool_left_sender = rte_pktmbuf_pool_create("pp_left_sender", left_sender_pool_size, PKTPOOL_CACHE, 0,
                                                 RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_left_sender));

  if (!pkt_pool_left_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Left Sender, Tester exits." << std::endl;
    return -1;
  }

  pkt_pool_right_receiver = rte_pktmbuf_pool_create("pp_right_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                    RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_right_receiver));
  if (!pkt_pool_right_receiver)
  {
    std::cerr << "Error: Cannot create packet pool for Right Receiver, Tester exits." << std::endl;
    return -1;
  }

  pkt_pool_right_sender = rte_pktmbuf_pool_create("pp_right_sender", right_sender_pool_size, PKTPOOL_CACHE, 0,
                                                  RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_right_sender));
  if (!pkt_pool_right_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_left_receiver = rte_pktmbuf_pool_create("pp_left_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                   RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_left_receiver));
  if (!pkt_pool_left_receiver)
  {
    std::cerr << "Error: Cannot create packet pool for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // set up the TX/RX queues
  if (rte_eth_tx_queue_setup(leftport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL) < 0)
  {
    std::cerr << "Error: Cannot setup TX queue for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  
  if (rte_eth_rx_queue_setup(rightport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL, pkt_pool_right_receiver) < 0)
  {
    std::cerr << "Error: Cannot setup RX queue for Right Receiver, Tester exits." << std::endl;
    return -1;
  }
    
  if (rte_eth_tx_queue_setup(rightport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL) < 0)
  {
    std::cerr << "Error: Cannot setup TX queue for Right Sender, Tester exits." << std::endl;
    return -1;
  } 
  
  if (rte_eth_rx_queue_setup(leftport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL, pkt_pool_left_receiver) < 0)
  {
    std::cerr << "Error: Cannot setup RX queue for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // start the Ethernet ports
  if (rte_eth_dev_start(leftport) < 0)
  {
    std::cerr << "Error: Cannot start network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }
  
  if (rte_eth_dev_start(rightport) < 0)
  {
    std::cerr << "Error: Cannot start network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  if (promisc)
  {
    rte_eth_promiscuous_enable(leftport);
    rte_eth_promiscuous_enable(rightport);
  }

  // check links' states (wait for coming up), try maximum MAX_PORT_TRIALS times
  trials = 0;
  do
  {
    if (trials++ == MAX_PORT_TRIALS)
    {
      std::cerr << "Error: Left Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
    rte_eth_link_get(leftport, &link_info);
  } while (link_info.link_status == RTE_ETH_LINK_DOWN);
  trials = 0;
  do
  {
    if (trials++ == MAX_PORT_TRIALS)
    {
      std::cerr << "Error: Right Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
    rte_eth_link_get(rightport, &link_info);
  } while (link_info.link_status == RTE_ETH_LINK_DOWN);

  // Some sanity checks: NUMA node of the cores and of the NICs are matching or not...
  if (numa_available() == -1)
    std::cout << "Info: This computer does not support NUMA." << std::endl;
  else
  {
    if (numa_num_configured_nodes() == 1)
      std::cout << "Info: Only a single NUMA node is configured, there is no possibilty for mismatch." << std::endl;
    else
    {
      if (forward)
      {
        numaCheck(leftport, "Left", cpu_left_sender, "Left Sender");
        numaCheck(rightport, "Right", cpu_right_receiver, "Right Receiver");
      }
      if (reverse)
      {
        numaCheck(rightport, "Right", cpu_right_sender, "Right Sender");
        numaCheck(leftport, "Left", cpu_left_receiver, "Left Receiver");
      }
    }
  }

  // Some sanity checks: TSCs of the used cores are synchronized or not...
  if (forward)
  {
    check_tsc(cpu_left_sender, "Left Sender");
    check_tsc(cpu_right_receiver, "Right Receiver");
  }
  if (reverse)
  {
    check_tsc(cpu_right_sender, "Right Sender");
    check_tsc(cpu_left_receiver, "Left Receiver");
  }

  // prepare further values for testing
  hz = rte_get_timer_hz();                                                       // number of clock cycles per second
  start_tsc = rte_rdtsc() + hz * START_DELAY / 1000;                             // Each active sender starts sending at this time
  finish_receiving = start_tsc + hz * (test_duration + stream_timeout / 1000.0); // Each receiver stops at this time
  
  
  for(int i = 0; i < tmp_lwb4data.size(); i++){
    num_of_port_sets = pow(2.0, tmp_lwb4data.at(i).psid_length);
    num_of_ports = (int)(65536.0 / num_of_port_sets);

    if (tmp_lwb4data.at(i).psid == 0){
      tmp_lwb4data.at(i).min_port = 0;
    }else {
      tmp_lwb4data.at(i).min_port = (num_of_ports * tmp_lwb4data.at(i).psid);
    }

    if(tmp_lwb4data.at(i).min_port < 1024){
      std::cout << "Warning: System Ports SHOULD NOT be allocated to lwB4s"  << std::endl;
    } else if(tmp_lwb4data.at(i).min_port > 65535){
      std::cerr << "Minimum port for lwB4 can't be greater than 65535"  << std::endl;
      return -1;
    }
    
    tmp_lwb4data.at(i).max_port = tmp_lwb4data.at(i).min_port + (num_of_ports -1);

    if(tmp_lwb4data.at(i).max_port < 1024){
      std::cout << "Warning: System Ports SHOULD NOT be allocated to lwB4s"  << std::endl;
    }else if(tmp_lwb4data.at(i).max_port > 65535){
      std::cerr << "Maximum port for lwB4 can't be greater than 65535" << std::endl;
      return -1;
    }
    tmp_lwb4data.at(i).ipv4_addr_chksum = rte_raw_cksum(&tmp_lwb4data.at(i).ipv4_addr,4); //calculate the IPv4 header checksum  
  }

  //delete lwB4s that are using system ports
  if (system_ports == 0){
    std::vector<int> index;
    for(int i = 0; i < tmp_lwb4data.size(); i++){
      if (tmp_lwb4data.at(i).min_port < 1023 || tmp_lwb4data.at(i).max_port < 1023)
      {
        index.push_back(i);
      }
    }
    
    
    sort(index.rbegin(), index.rend());
    for (int i : index){ std::cout << i << " " << std::endl ;}
    for(int i : index){
      std::cout << "System ports are not allowed! Deleting lwb4 with psdi " << tmp_lwb4data.at(i).psid << std::endl;
      std::cout << "Deleted Min port " << tmp_lwb4data.at(i).min_port << std::endl;
      std::cout << "Deleted Max port " << tmp_lwb4data.at(i).max_port << std::endl;

      tmp_lwb4data.erase(tmp_lwb4data.begin() + i);
    }
  }

  thread_local std::random_device rd;
  thread_local std::mt19937 gen {rd()};
  std::ranges::shuffle(tmp_lwb4data, gen);

  // allocate and build a memory to store the data of all possible simulated LwB4s.
  //alloc memory in the numa node what LEFTPORT using
  lwB4_array = (lwB4_data *)rte_malloc_socket("LwB4s data memory", tmp_lwb4data.size() * sizeof(lwB4_data), 0, rte_eth_dev_socket_id(LEFTPORT)); 
  
  if (!lwB4_array){
    std::cerr <<  "malloc failure!! Can not create memory for lwB4 data" << std::endl;
    return -1;
  }

  for(int i = 0; i < tmp_lwb4data.size(); i++){
    //For easier debugging if needed
    //std::cout << "PSID " << tmp_lwb4data.at(i).psid << std::endl;
    //std::cout << "Min port " << tmp_lwb4data.at(i).min_port << std::endl;
    //std::cout << "Max port " << tmp_lwb4data.at(i).max_port << std::endl;
    lwB4_array[i] = tmp_lwb4data.at(i);
  }

  return 0;
} //end init

//checks NUMA localty: is the NUMA node of network port and CPU the same?
void Throughput::numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name) {
  int n_port, n_cpu;
  n_port = rte_eth_dev_socket_id(port);
  n_cpu = numa_node_of_cpu(cpu);
  if ( n_port == n_cpu )
    std::cout << "Info: " << port_side << " port and " << cpu_name << " CPU core belong to the same NUMA node: " << n_port << std::endl;
  else
    std::cout << "Warning: " << port_side << " port and " << cpu_name << " CPU core belong to NUMA nodes " <<
      n_port << ", " << n_cpu << ", respectively." << std::endl; 
}


struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, uint32_t *dst_ip)
{
  // printf("inside mkTestFrame4: the beginning\n");
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf){
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    //std::cerr << "Error: " << direction << " sender can't allocate a new mbuf for the Test Frame!" << std::endl;
    //return -1;
  }
  length -= RTE_ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr));                                      // IPv4 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));                     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800); // contains an IPv4 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip); // Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(rte_ipv4_hdr);   // No IP Options are used
  mkUdpHeader(udp_hd, udp_length); // sets UPD port numbers and checksum to 0.
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_length);
  // udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hd); // UDP checksum is calculated and set
  // The line above caused problem because the final step of the calculation was not reversible
  // To be able to manipulate the UDP checksum later, the uncomplemented UDP checksum is stored below:
  uint32_t cksum = rte_raw_cksum(udp_hd, udp_length);
  cksum += rte_ipv4_phdr_cksum(ip_hdr, 0);
  cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);
  cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);  // twice must be enough
  udp_hd->dgram_cksum = (uint16_t)cksum;    // The uncomplemented UDP checksum is stored (for further processing).
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);               // IPv4 header checksum is set now
  return pkt_mbuf;
}

void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type)
{
  rte_memcpy(&eth->dst_addr, dst_mac, sizeof(struct rte_ether_hdr));
  rte_memcpy(&eth->src_addr, src_mac, sizeof(struct rte_ether_hdr));
  eth->ether_type = htons(ether_type);
}

void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, uint32_t *dst_ip)
{
  ip->version_ihl = 0x45; // Version: 4, IHL: 20/4=5
  ip->type_of_service = 0;
  ip->total_length = htons(length);
  ip->packet_id = 0;
  ip->fragment_offset = 0;
  ip->time_to_live = 0x0A;
  ip->next_proto_id = 0x11; // UDP
  ip->hdr_checksum = 0;
  rte_memcpy(&ip->src_addr, src_ip, 4);
  rte_memcpy(&ip->dst_addr, dst_ip, 4);
  // May NOT be set now, only after the UDP header checksum calculation: ip->hdr_checksum = rte_ipv4_cksum(ip);
}

// creates a UDP header
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length)
{
  udp->src_port = 0;
  udp->dst_port = 0;
  udp->dgram_len = htons(length);
  udp->dgram_cksum = 0; // Checksum is set to 0 now.
  // UDP checksum is calculated later.
}

// creates and IPv6 header
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, struct in6_addr *src_ip, struct in6_addr *dst_ip, uint8_t proto)
{
  ip->vtc_flow = htonl(0x60000000); // Version: 6, Traffic class: 0, Flow label: 0
  ip->payload_len = htons(length - sizeof(rte_ipv6_hdr));
  ip->proto = proto; // 0x04 for IPIP, 0x11 for UDP
  ip->hop_limits = 0x0A;
  rte_mov16((uint8_t *)&ip->src_addr, (uint8_t *)src_ip);
  rte_mov16((uint8_t *)&ip->dst_addr, (uint8_t *)dst_ip);
}

// creates the tunneled Test Frame using several helper functions
struct rte_mbuf *mkTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6, 
                              const uint32_t *src_ipv4, uint32_t *dst_ipv4)
{
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf){
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    //std::cerr << "Error: " << direction << " sender can't allocate a new mbuf for the Test Frame!" << std::endl;
    //return -1;
  }
  length -= RTE_ETHER_CRC_LEN;
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv6_hdr *ipv6_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr));                        // IPv6 header
  rte_ipv4_hdr *ipv4_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr));    // IPv4 header                                      
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_ipv4_hdr));     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)); // UDP data
  
  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ipv6_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ipv6_hdr, ipv6_length, src_ipv6, dst_ipv6, 0x04); //0x04 for IPIP
  int ipv4_length = ipv6_length - sizeof(rte_ipv6_hdr);
  mkIpv4Header(ipv4_hdr, ipv4_length, src_ipv4, dst_ipv4); // Does not set IPv4 header checksum
  int udp_length = ipv4_length - sizeof(rte_ipv4_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_length);
  // udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4_hdr, udp_hd); // UDP checksum is calculated and set
  // The line above caused problem because the final step of the calculation was not reversible
  // To be able to manipulate the UDP checksum later, the uncomplemented UDP checksum is stored below:
  uint32_t cksum = rte_raw_cksum(udp_hd, udp_length);
  cksum += rte_ipv4_phdr_cksum(ipv4_hdr, 0);
  cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);
  cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);  // twice must be enough
  udp_hd->dgram_cksum = (uint16_t)cksum;    // The uncomplemented UDP checksum is stored (for further processing).
  ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr); 
  return pkt_mbuf;
}

struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ip, struct in6_addr *dst_ip)
{
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf){
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    //std::cerr << "Error: " << direction << " sender can't allocate a new mbuf for the Test Frame!" << std::endl;
    //return -1;
  }
  length -= RTE_ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv6_hdr *ip_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr));                                      // IPv6 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr));                     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ip_hdr, ip_length, src_ip, dst_ip, 0x11); //0x11 for UDP
  int udp_length = ip_length - sizeof(rte_ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_length);
  udp_hd->dgram_cksum = ~rte_ipv6_udptcp_cksum(ip_hdr, udp_hd); // the uncomplemented UDP checksum is calculated and set
  return pkt_mbuf;
}

// fills the data field of the Test Frame
void mkData(uint8_t *data, uint16_t length)
{
  unsigned i;
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identification of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  for (i = 0; i < length; i++)
    data[i] = i % 256;
}

// calculates sender pool size, it is a virtual member function, redefined in derived classes
int Throughput::senderPoolSize()
{
  return 2 * N + PORT_TX_QUEUE_SIZE + 100; // 2*: fg. and bg. Test Frames
  // if varport then everything exists in N copies, see the definition of N
}

// performs throughput (or frame loss rate) measurement
void Throughput::measure(uint16_t leftport, uint16_t rightport) {
  time_t now; // needed for printing out a timestamp in Info message

  //std::cout << "measure runs on CPU core: " << rte_lcore_id() << std::endl;
  
  senderCommonParameters scp;
  senderParameters spars, spars2;
  receiverParameters rpars, rpars2;

  scp = senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,test_duration,n,m,hz,start_tsc,
                               number_of_lwB4s,select_lwB4,lwB4_array,&ipv6_tunnel,&ipv4_server,
                               &ipv6_left_bg,&ipv6_right_bg,
                               fwd_sport_min, fwd_sport_max, fwd_dport_min, fwd_dport_max,
                               rev_sport_min, rev_sport_max, rev_dport_min, rev_dport_max);

  
  if ( forward )
  { // Left to right direction is active

    // set individual parameters for the left sender
    // Initialize the parameter class instance
    spars = senderParameters(&scp, pkt_pool_left_sender, leftport, "forward", (ether_addr *)mac_left_dut, (ether_addr *)mac_left_tester);

    // start left sender
    if (rte_eal_remote_launch(send6, &spars, cpu_left_sender))
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    rpars = receiverParameters(finish_receiving, rightport, "forward");

    // start right receiver
    if (rte_eal_remote_launch(receive, &rpars, cpu_right_receiver))
      std::cout << "Error: could not start Right Receiver." << std::endl;
  }

  if ( reverse ) 
  { // Right to Left direction is active

    // set individual parameters for the right sender
    // Initialize the parameter class instance
    spars2 = senderParameters(&scp, pkt_pool_right_sender, rightport, "reverse", (ether_addr *)mac_right_dut, (ether_addr *)mac_right_tester);

    // start right sender
    if (rte_eal_remote_launch(send4, &spars2, cpu_right_sender))
      std::cout << "Error: could not start Right Sender." << std::endl;

    // set parameters for the left receiver
    rpars2 = receiverParameters(finish_receiving, leftport, "reverse");

    // start left receiver
    if (rte_eal_remote_launch(receive, &rpars2, cpu_left_receiver))
      std::cout << "Error: could not start Left Receiver." << std::endl; 
  }

  now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

  // wait until active senders and receivers finish
  if ( forward )
  {
    rte_eal_wait_lcore(cpu_left_sender);
    rte_eal_wait_lcore(cpu_right_receiver);
  }
  if ( reverse )
  {
    rte_eal_wait_lcore(cpu_right_sender);
    rte_eal_wait_lcore(cpu_left_receiver);
  }

  rte_free(lwB4_array); // release the CEs data memory

  std::cout << "Info: Test finished." << std::endl;
}

// sets the values of the data fields
senderCommonParameters::senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                                              uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, unsigned select_lwB4_, lwB4_data *lwB4_array_,
                                              struct in6_addr *ipv6_tunnel_, uint32_t *ipv4_server_, struct in6_addr *ipv6_left_bg_, struct in6_addr *ipv6_right_bg_,
                        uint16_t fwd_sport_min_, uint16_t fwd_sport_max_, uint16_t fwd_dport_min_, uint16_t fwd_dport_max_,
                        uint16_t rev_sport_min_, uint16_t rev_sport_max_, uint16_t rev_dport_min_, uint16_t rev_dport_max_)
{

  ipv6_frame_size = ipv6_frame_size_;
  ipv4_frame_size = ipv4_frame_size_;
  frame_rate = frame_rate_;
  test_duration = test_duration_;
  n = n_; //??
  m = m_; //??
  hz = hz_;
  start_tsc = start_tsc_;
  number_of_lwB4s = number_of_lwB4s_;
  select_lwB4 = select_lwB4_;
  lwB4_array = lwB4_array_;
  ipv6_tunnel = ipv6_tunnel_;
  ipv6_left_bg = ipv6_left_bg_; 
  ipv6_right_bg = ipv6_right_bg_;
  ipv6_tunnel = ipv6_tunnel_;
  ipv4_server = ipv4_server_;
  fwd_sport_min = fwd_sport_min_;
  fwd_sport_max = fwd_sport_max_;
  fwd_dport_min = fwd_dport_min_;
  fwd_dport_max = fwd_dport_max_;
  rev_sport_min = rev_sport_min_;
  rev_sport_max = rev_sport_max_;
  rev_dport_min = rev_dport_min_;
  rev_dport_max = rev_dport_max_;
}

senderCommonParameters::senderCommonParameters(){}


// sets the values of the data fields
senderParameters::senderParameters(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                                  struct ether_addr *dst_mac_, struct ether_addr *src_mac_)
{
  cp = cp_;
  pkt_pool = pkt_pool_;
  eth_id = eth_id_;
  direction = direction_;
  dst_mac = dst_mac_;
  src_mac = src_mac_;
}

senderParameters::senderParameters(){}

// sets the values of the data fields
receiverParameters::receiverParameters(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_)
{
  finish_receiving = finish_receiving_;
  eth_id = eth_id_;
  direction = direction_;
}
receiverParameters::receiverParameters(){}

// sends IPv6 Test Frames containing IPv4 Test Frames for throughput (or frame loss rate) measurement
int send6(void *par)
{
  //  collecting input parameters:
  class senderParameters *p = (class senderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t test_duration = cp->test_duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;
  uint32_t num_of_lwB4s = cp->number_of_lwB4s;
  unsigned select_lwB4 = cp->select_lwB4;
  lwB4_data *lwB4_array = cp->lwB4_array;
  uint16_t fwd_sport_min = cp->fwd_sport_min; 
  uint16_t fwd_sport_max = cp->fwd_sport_max;
  uint16_t fwd_dport_min = cp->fwd_dport_min; 
  uint16_t fwd_dport_max = cp->fwd_dport_max; 
  uint16_t rev_sport_min = cp->rev_sport_min; 
  uint16_t rev_sport_max = cp->rev_sport_max;
  uint16_t rev_dport_min = cp->rev_dport_min; 
  uint16_t rev_dport_max = cp->rev_dport_max; 

  struct in6_addr *ipv6_tunnel = cp->ipv6_tunnel;
  uint32_t *ipv4_server = cp->ipv4_server;
  struct in6_addr *ipv6_left_bg = cp->ipv6_left_bg;
  struct in6_addr *ipv6_right_bg = cp->ipv6_right_bg;

  // parameters which are different for the Left sender and the Right sender
  rte_mempool *pkt_pool = p->pkt_pool;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  
  // further local variables
  uint64_t frames_to_send = test_duration * frame_rate; // Each active sender sends this number of frames
  uint64_t sent_frames = 0;                             // counts the number of sent frames
  double elapsed_seconds;                               // for checking the elapsed seconds during sending
    
  // all zero IP addresses will be put in the template packets to be able to genereate correct checksum
  uint32_t zero_ipv4;
  struct in6_addr zero_ipv6;

  if (inet_pton(AF_INET, "0.0.0.0", reinterpret_cast<void *>(&zero_ipv4)) != 1)
  {
    std::cerr << "Input Error: Bad zero_ipv4 address." << std::endl;
    return -1;
  }
  
  if (inet_pton(AF_INET6, "::", reinterpret_cast<void *>(&zero_ipv6)) != 1)
  {
    std::cerr << "Input Error: Bad zero_ipv6 address." << std::endl;
    return -1;
  }
  
  // check whether the lwB4 array is built or not
  if(!lwB4_array){
    std::cerr << "No lwB4 array can be accessed by the sender" << std::endl;
    return -1;
  }
    
  
  // implementation of pseudorandom port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
  // always one of the same N pre-prepared foreground or background frames is updated and sent,
  // N size arrays are used to resolve the write after send problem

  //some worker variables
  int i;                        // cycle variable for the above mentioned purpose: takes {0..N-1} values
  int current_lwB4;             // index variable to the currently simulated lwB4 in the lwB4_array
  uint16_t psid;                // PSID of the currently simulated lwB4
  struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
  uint8_t *pkt;                 // working pointer to the current frame (in the message buffer)
  
  //IP workers
  uint32_t *fg_dst_ipv4[N], *fg_src_ipv4[N], *fg_dst_tun_ipv4[N], *fg_src_tun_ipv4[N];
  struct in6_addr *fg_src_ipv6[N], *fg_dst_ipv6[N];
  struct in6_addr *bg_src_ipv6[N], *bg_dst_ipv6[N];
  uint16_t *fg_ipv4_chksum[N], *fg_tun_ipv4_chksum[N];
  
  //UDP workers
  uint16_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; 
  uint16_t *udp_sport, *udp_dport, *udp_chksum;   

  uint16_t fg_udp_chksum_start, bg_udp_chksum_start, fg_ipv4_chksum_start, fg_tun_ipv4_chksum_start; // starting values (uncomplem.'d)                  
  uint32_t chksum = 0;          // temporary variable for UDP checksum calculation
  uint32_t ip_chksum = 0;       // temporary variable for IPv4 header checksum calculation
  uint16_t sp, dp;              // values of source and destination port numbers -- temporary values
  uint16_t tunneled_frame_size =  ipv6_frame_size + 20;
  
  // creating buffers of template test frames
  for (i = 0; i < N; i++)
  {
    // create a foreground Test Frame 
    fg_pkt_mbuf[i] = mkTestIpv4inIpv6Tun(tunneled_frame_size,pkt_pool,"Forward",dst_mac,src_mac,&zero_ipv6,ipv6_tunnel,&zero_ipv4,ipv4_server);
    pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *);
    fg_src_ipv6[i] = (struct in6_addr *)(pkt + 22);
    fg_tun_ipv4_chksum[i] = (uint16_t *)(pkt + 64);
    fg_tun_ipv4_chksum_start = ~*fg_tun_ipv4_chksum[i];
    fg_src_tun_ipv4[i] = (uint32_t *)(pkt + 66);
    fg_udp_sport[i] = (uint16_t *)(pkt + 74);
    fg_udp_dport[i] = (uint16_t *)(pkt + 76);
    fg_udp_chksum[i] = (uint16_t *)(pkt + 80);
    fg_udp_chksum_start = *fg_udp_chksum[i]; // save the uncomplemented UDP checksum value (same for all values of "i")

    // Create a backround Test Frame (an IPv6 frame)
    bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv6_left_bg,ipv6_right_bg);
    pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    bg_udp_sport[i] = (uint16_t *)(pkt + 54);
    bg_udp_dport[i] = (uint16_t *)(pkt + 56);
    bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
  }
  
  //save the uncomplemented UDP checksum value (same for all values of [i]). So, [0] is enough
  fg_udp_chksum_start = *fg_udp_chksum[0]; // for the foreground frames 
  bg_udp_chksum_start = *bg_udp_chksum[0]; // same but for the background frames
  
  fg_tun_ipv4_chksum_start = ~*fg_tun_ipv4_chksum[0]; // save the uncomplemented IPv4 header checksum 
  
  // prepare random number infrastructure
  thread_local std::random_device rd;                   // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_sport(rd());         // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::mt19937_64 gen_dport(rd());         // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::mt19937_64 gen_lwB4_index(rd());    // Standard 64-bit mersenne_twister_engine seeded with rd()

  std::uniform_int_distribution<int> uni_dis_lwB4_index(0,num_of_lwB4s-1);

  switch ( select_lwB4 )
  {
  case 0: // always use lwB4_array[0]
    current_lwB4 = 0;
    break;
  case 1: // use the elements of the 'lwB4_array' in increasing order
    current_lwB4 = 0; // increase maunally after each sending
    break;
  case 2: // use the elements of the 'lwB4_array' in decreasing order
    current_lwB4 = num_of_lwB4s-1; // decrease maunally after each sending
    break;
  case 3: // select an element from the 'lwB4_array' in a pseudorandom way
    current_lwB4 = uni_dis_lwB4_index(gen_lwB4_index);
    break;
  }

  // naive sender version: it is simple and fast
  i = 0; // increase maunally after each sending
  for (sent_frames = 0; sent_frames < frames_to_send; sent_frames++)
  { // Main cycle for the number of frames to send
    bool IsUDPoverIPv4; 	// It is true for foreground frames, and false for background frames.
    // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
    if ( IsUDPoverIPv4 = sent_frames % n < m )
    {
      // foreground frame is to be sent
      psid = lwB4_array[current_lwB4].psid;
      chksum = fg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = fg_udp_sport[i];
      udp_dport = fg_udp_dport[i];
      udp_chksum = fg_udp_chksum[i];
      pkt_mbuf = fg_pkt_mbuf[i];

      //Set the IPv4 packet fields, IP addresses, checksum
      ip_chksum = fg_tun_ipv4_chksum_start; // restore the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
      *fg_src_tun_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; // set it with the lwB4's IPv4 address
      chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;      // add its chechsum to the UDP checksum
      ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;   // and to the IPv4 header checksum

      ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
      ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
      ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
      *fg_tun_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

      *fg_src_ipv6[i] = lwB4_array[current_lwB4].b4_ipv6_addr; // set it with the right address

      std::uniform_int_distribution<int> uni_dis_sport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port);
      sp = uni_dis_sport(gen_sport);
      chksum += *udp_sport = htons(sp); // set the source port and add it to the UDP checksum

      std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max);
      dp = uni_dis_dport(gen_dport);
      chksum += *udp_dport = htons(dp); // set the destination port and add it to the UDP checksum
    }
    else
    {
      // background frame is to be sent
      // from here, we need to handle the background frame identified by the temporary variables
      chksum = bg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = bg_udp_sport[i];
      udp_dport = bg_udp_dport[i];
      udp_chksum = bg_udp_chksum[i];
      pkt_mbuf = bg_pkt_mbuf[i];
  
      std::uniform_int_distribution<int> uni_dis_sport(fwd_sport_min,fwd_sport_max);
      sp = uni_dis_sport(gen_sport);
      chksum += *udp_sport = htons(sp); // set the source port and add it to the UDP checksum
   
      std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max);
      dp = uni_dis_dport(gen_dport);
      chksum += *udp_dport = htons(dp); // set the destination port and add it to the UDP checksum
    }

    //finalize the UDP checksum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = (~chksum) & 0xffff;                                // make one's complement
    if ( unlikely( IsUDPoverIPv4 && chksum == 0 ) )             // over IPv4, checksum should not be 0 (0 means, no checksum is used)
      chksum = 0xffff;
    *udp_chksum = (uint16_t)chksum; // set the UDP checksum in the frame

    // finally, send the frame
    while (rte_rdtsc() < start_tsc + sent_frames * hz / frame_rate)
      ; // Beware: an "empty" loop, as well as in the next line
    while (!rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1))
      ; // send out the frame

    switch ( select_lwB4 )
    {
    case 0: // always use lwB4_array[0]
      break;
    case 1: // use the elements of the 'lwB4_array' in increasing order
      current_lwB4 = (current_lwB4 + 1) % num_of_lwB4s;
      break;
    case 2: // use the elements of the 'lwB4_array' in decreasing order
      if ( !current_lwB4-- ) 
        current_lwB4 = num_of_lwB4s-1;
      break;
    case 3: // select an element from the 'lwB4_array' in a pseudorandom way
      current_lwB4 = uni_dis_lwB4_index(gen_lwB4_index);
      break;
    }

    i = (i + 1) % N;
  } // this is the end of the sending cycle

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc() - start_tsc) / hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", direction, elapsed_seconds);
  if (elapsed_seconds > test_duration * TOLERANCE){
    std::cout << direction << " sending exceeded the " << test_duration * TOLERANCE << " seconds limit, the test is invalid." << std::endl;
    return -1;
  }
  printf("%s frames sent: %lu\n", direction, sent_frames);  

  return 0;
}


// sends IPv4 Test Frames for throughput (or frame loss rate) measurement
int send4(void *par)
{
  //std::cout << "Send STARTED on CPU core: " << rte_lcore_id() << " Using NUMA node: " << rte_socket_id() << std::endl;
  
  //  collecting input parameters:
  class senderParameters *p = (class senderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t test_duration = cp->test_duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;
  uint32_t num_of_lwB4s = cp->number_of_lwB4s;
  unsigned select_lwB4 = cp->select_lwB4;
  lwB4_data *lwB4_array = cp->lwB4_array;
  uint16_t fwd_sport_min = cp->fwd_sport_min;
  uint16_t fwd_sport_max = cp->fwd_sport_max;
  uint16_t fwd_dport_min = cp->fwd_dport_min;
  uint16_t fwd_dport_max = cp->fwd_dport_max;
  uint16_t rev_sport_min = cp->rev_sport_min;
  uint16_t rev_sport_max = cp->rev_sport_max;
  uint16_t rev_dport_min = cp->rev_dport_min;
  uint16_t rev_dport_max = cp->rev_dport_max;

  struct in6_addr *ipv6_tunnel = cp->ipv6_tunnel;
  uint32_t *ipv4_server = cp->ipv4_server;
  struct in6_addr *ipv6_left_bg = cp->ipv6_left_bg;
  struct in6_addr *ipv6_right_bg = cp->ipv6_right_bg;

  // parameters which are different for the Left sender and the Right sender
  rte_mempool *pkt_pool = p->pkt_pool;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  
  // further local variables
  uint64_t frames_to_send = test_duration * frame_rate; // Each active sender sends this number of frames
  uint64_t sent_frames = 0;                             // counts the number of sent frames
  double elapsed_seconds;                               // for checking the elapsed seconds during sending

  // all zero IP addresses will be put in the template packets to be able to genereate correct checksum
  uint32_t zero_ipv4;

  if (inet_pton(AF_INET, "0.0.0.0", reinterpret_cast<void *>(&zero_ipv4)) != 1)
  {
    std::cerr << "Input Error: Bad zero_ipv4 address." << std::endl;
    return -1;
  }
  
  // check whether the lwB4 array is built or not
  if(!lwB4_array){
    std::cerr << "No lwB4 array can be accessed by the sender" << std::endl;
    return -1;
  }
    
  // implementation of pseudorandom port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
  // always one of the same N pre-prepared foreground or background frames is updated and sent,
  // N size arrays are used to resolve the write after send problem

  //some worker variables
  int i;                        // cycle variable for the above mentioned purpose: takes {0..N-1} values
  int current_lwB4;             // index variable to the currently simulated lwB4 in the lwB4_array
  uint16_t psid;                // PSID of the currently simulated lwB4
  struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
  uint8_t *pkt;                 // working pointer to the current frame (in the message buffer)
  
  //IP workers
  uint32_t *fg_dst_ipv4[N], *fg_src_ipv4[N];
  struct in6_addr *bg_src_ipv6[N], *bg_dst_ipv6[N];
  uint16_t *fg_ipv4_chksum[N];
  
  //UDP workers
  uint16_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; 
  uint16_t *udp_sport, *udp_dport, *udp_chksum;   

  uint16_t fg_udp_chksum_start, bg_udp_chksum_start, fg_ipv4_chksum_start; // starting values (uncomplemented checksums)                  
  uint32_t chksum = 0;          // temporary variable for UDP checksum calculation
  uint32_t ip_chksum = 0;       // temporary variable for IPv4 header checksum calculation
  uint16_t sp, dp;              // values of source and destination port numbers -- temporary values

  // creating buffers of template test frames
 for (i = 0; i < N; i++)
  {
    // create a foreground Test Frame
    // the source IPv4 address will be permanantly the the Tester's right IPv4 address ("IPv4-server")
    fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size,pkt_pool,"Reverse",dst_mac,src_mac,ipv4_server,&zero_ipv4);
    pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    fg_ipv4_chksum[i] = (uint16_t *)(pkt + 24);
    fg_ipv4_chksum_start = ~*fg_ipv4_chksum[i]; // save the uncomplementd checksum 
    fg_dst_ipv4[i] = (uint32_t *)(pkt + 30); // The destination IPv4 address will come from lwB4 data each time
    fg_udp_sport[i] = (uint16_t *)(pkt + 34);
    fg_udp_dport[i] = (uint16_t *)(pkt + 36); // Need to change based on lwB4 data
    fg_udp_chksum[i] = (uint16_t *)(pkt + 40);
    fg_udp_chksum_start = *fg_udp_chksum[i]; // save the uncomplemented UDP checksum value (same for all values of "i")

    // Create a backround Test Frame (an IPv6 frame)
    bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv6_right_bg,ipv6_left_bg);
    pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    bg_udp_sport[i] = (uint16_t *)(pkt + 54);
    bg_udp_dport[i] = (uint16_t *)(pkt + 56);
    bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
  }
  
  //save the uncomplemented UDP checksum value (same for all values of [i]). So, [0] is enough
  fg_udp_chksum_start = *fg_udp_chksum[0]; // for the foreground frames 
  bg_udp_chksum_start = *bg_udp_chksum[0]; // same but for the background frames
  
  // save the uncomplemented IPv4 header checksum (same for all values of [i]). So, [0] is enough
  fg_ipv4_chksum_start = ~*fg_ipv4_chksum[0]; 
  
  // prepare random number infrastructure
  thread_local std::random_device rd;                   // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_sport(rd());         // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::mt19937_64 gen_dport(rd());         // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::mt19937_64 gen_lwB4_index(rd());    // Standard 64-bit mersenne_twister_engine seeded with rd()

  std::uniform_int_distribution<int> uni_dis_lwB4_index(0,num_of_lwB4s-1);

  switch ( select_lwB4 )
  {
  case 0: // always use lwB4_array[0]
    current_lwB4 = 0;
    break;
  case 1: // use the elements of the 'lwB4_array' in increasing order
    current_lwB4 = 0; // increase maunally after each sending
    break;
  case 2: // use the elements of the 'lwB4_array' in decreasing order
    current_lwB4 = num_of_lwB4s-1; // decrease maunally after each sending
    break;
  case 3: // select an element from the 'lwB4_array' in a pseudorandom way
    current_lwB4 = uni_dis_lwB4_index(gen_lwB4_index);
    break;
  }

  // naive sender version: it is simple and fast
  i = 0; // increase maunally after each sending
  for (sent_frames = 0; sent_frames < frames_to_send; sent_frames++)
  { // Main cycle for the number of frames to send
    bool IsUDPoverIPv4; 	// It is true for foreground frames, and false for background frames.
    // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
    if ( IsUDPoverIPv4 = sent_frames % n < m )
    {
      // foreground frame is to be sent
      psid = lwB4_array[current_lwB4].psid;
      chksum = fg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = fg_udp_sport[i];
      udp_dport = fg_udp_dport[i];
      udp_chksum = fg_udp_chksum[i];
      pkt_mbuf = fg_pkt_mbuf[i];

      ip_chksum = fg_ipv4_chksum_start; // uncomplemented IPv4 header checksum to add the checksum value of the dst. IPv4 address

      *fg_dst_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; //set it with the LwB4's IPv4 address
      chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //add its chechsum to the UDP checksum
      ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //and to the IPv4 header checksum

      ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
      ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
      ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
      *fg_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

      std::uniform_int_distribution<int> uni_dis_dport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port);
      dp = uni_dis_dport(gen_dport);
      chksum += *udp_dport = htons(dp); // set the destination port and add it to the UDP checksum

      std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max);
      sp = uni_dis_sport(gen_sport);
      chksum += *udp_sport = htons(sp); // set the source port and add it to the UDP checksum
    }
    else
    {
      // background frame is to be sent
      chksum = bg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = bg_udp_sport[i];
      udp_dport = bg_udp_dport[i];
      udp_chksum = bg_udp_chksum[i];
      pkt_mbuf = bg_pkt_mbuf[i];
  
      std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max);
      sp = uni_dis_sport(gen_sport);
      chksum += *udp_sport = htons(sp); // set the source port and add it to the UDP checksum
   
      std::uniform_int_distribution<int> uni_dis_dport(rev_dport_min,rev_dport_max);
      dp = uni_dis_dport(gen_dport);
      chksum += *udp_dport = htons(dp); // set the destination port and add it to the UDP checksum
    }

    //finalize the UDP checksum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = (~chksum) & 0xffff;                                // make one's complement
    if ( unlikely( IsUDPoverIPv4 && chksum == 0 ) )             // over IPv4, checksum should not be 0 (0 means, no checksum is used)
      chksum = 0xffff;
    *udp_chksum = (uint16_t)chksum; // set the UDP checksum in the frame

    // finally, send the frame
    while (rte_rdtsc() < start_tsc + sent_frames * hz / frame_rate)
      ; // Beware: an "empty" loop, as well as in the next line
    while (!rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1))
      ; // send out the frame

    switch ( select_lwB4 )
    {
    case 0: // always use lwB4_array[0]
      break;
    case 1: // use the elements of the 'lwB4_array' in increasing order
      current_lwB4 = (current_lwB4 + 1) % num_of_lwB4s;
      break;
    case 2: // use the elements of the 'lwB4_array' in decreasing order
      if ( !current_lwB4-- )
        current_lwB4 = num_of_lwB4s-1;
      break;
    case 3: // select an element from the 'lwB4_array' in a pseudorandom way
      current_lwB4 = uni_dis_lwB4_index(gen_lwB4_index);
      break;
    }

    i = (i + 1) % N;
  } // this is the end of the sending cycle

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc() - start_tsc) / hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", direction, elapsed_seconds);
  if (elapsed_seconds > test_duration * TOLERANCE){
    std::cout << direction << " sending exceeded the " << test_duration * TOLERANCE << " seconds limit, the test is invalid." << std::endl;
    return -1;
  }
  printf("%s frames sent: %lu\n", direction, sent_frames);  

  return 0;
}


// receives Test Frames for throughput (or frame loss rate) measurements
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int receive(void *par)
{
  //std::cout << "Receive STARTED on CPU core: " << rte_lcore_id() << " Using NUMA node: " << rte_socket_id() << std::endl;
  
  // collecting input parameters:
  class receiverParameters *p = (class receiverParameters *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST];                      // pointers for the mbufs of received frames
  uint16_t ipv4 = htons(0x0800);                                  // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6 = htons(0x86DD);                                  // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identificion of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  uint64_t received = 0; // number of received frames

  while (rte_rdtsc() < finish_receiving)
  {
    
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);

    for (i = 0; i < frames; i++)
    { 
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message bufferq

      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if (*(uint16_t *)&pkt[12] == ipv6)
      { /* IPv6 */
        if (likely(pkt[20] == 4 && *(uint64_t *)&pkt[82] == *id)){
          /* check if IPv6 Next Header is IPIP, and the first 8 bytes of UDP data is 'IDENTIFY' */
          received++;
        }else if (likely(pkt[20] == 17 && *(uint64_t *)&pkt[62] == *id)){
          //IPv6
          received++;
        }
      }
      else if (*(uint16_t *)&pkt[12] == ipv4)
      { /* IPv4 */
         /* check if IPv4 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if (likely(pkt[23] == 17 && *(uint64_t *)&pkt[42] == *id))
          received++;
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  printf("%s frames received: %lu\n", direction, received);
  return received;
}

int Throughput::generate_lwB4Data(int argc, const char *argv[]){

  std::cout << "READ CMD STARTED" << std::endl;
  if (argc != 8)
  {
    std::cerr << "Input Error: Argument missmatch." << std::endl;
    std::cerr << "The correct format is: ./build/lw4o6_tester generate_lwB4Data <ipv4> <num_ipv4s> <br_address> <b4_ipv6> <psid_len> <output_file>" << std::endl;
    return -1;
  }

  //check paramas
  std::string ipv4 = (std::string)argv[2];
  int num_address = std::atoi(argv[3]);
  std::string br_address = (std::string)argv[4];
  std::string ipv6 = (std::string)argv[5];
  int psid_len = std::atoi(argv[6]);
  if (psid_len < 1 || psid_len > 16){
    std::cerr << "Input Error: psid_length must be between 1 and 16." << std::endl;
    return -1;
  }
  std::string file_name = (std::string)argv[7];

  std::ofstream outfile(file_name);
  if (!outfile) {
    std::cerr << "Failed to create output file\n";
    return 1;
  }
  
  std::string tmp_ipv4;
  std::string tmp_ipv6;
  int current_psid=0;
  int max_psid = int(pow(2,psid_len)) -1;
  
  for (int current_lwB4=0; current_lwB4< num_address; current_lwB4++){
    for(current_psid; current_psid <= max_psid; current_psid++){
      
      //lwB4 to the conf file
      outfile << "[ ]" << "\n";
      outfile << "ipv4 " << ipv4 << "\n";
      outfile << "b4-ipv6 " << ipv6 << "\n";
      outfile << "br-address " << br_address << "\n";
      outfile << "psid-length " << psid_len << "\n";
      outfile << "psid " << current_psid << "\n"; 

      //increase ipv6
      struct in6_addr addr;
      if (inet_pton(AF_INET6, ipv6.c_str(), &addr) != 1) {
        throw std::runtime_error("Invalid IPv6 address: " + ipv6);
      }

      for (int j = 15; j >= 0; --j) {  // carry
        if (++addr.s6_addr[j] != 0) break;
      }

      char buffer[INET6_ADDRSTRLEN];
      if (inet_ntop(AF_INET6, &addr, buffer, INET6_ADDRSTRLEN) == nullptr) {
        throw std::runtime_error("inet_ntop failed");
      }
      ipv6 = std::string(buffer);
    }

    //increase ipv4 cim
    struct in_addr addr;
    if (inet_pton(AF_INET, ipv4.c_str(), &addr) != 1) {
        throw std::runtime_error("Invalid IPv4 address: " + ipv4);
    }
    addr.s_addr = htonl(ntohl(addr.s_addr) + 1);

    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr, buffer, INET_ADDRSTRLEN) == nullptr) {
        throw std::runtime_error("inet_ntop failed");
    }
    ipv4 = std::string(buffer);

    //set psid to 0 for next ipv4
    current_psid = 0;
  }
    
  return 0;
}


