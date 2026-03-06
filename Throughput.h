#ifndef Throughput_H
#define Throughput_H

#include <cstdint>
#include <rte_mbuf.h>
#include <netinet/in.h>
#include <vector>

struct lwB4_data
{
public:
  uint32_t ipv4_addr;
  uint16_t ipv4_addr_chksum;
  uint32_t psid;
  uint32_t psid_length;
  struct in6_addr b4_ipv6_addr;
  struct in6_addr aftr_tunnel_addr;
  uint32_t min_port;
  uint32_t max_port;
};

class Throughput
{
public:
  // parameters from the configuration file
  uint32_t ipv4_server;         // Tester's right interface IPv4 address
  struct in6_addr ipv6_left_bg; // Tester's left interface IPv6 address (used for sending background traffic)
  struct in6_addr ipv6_right_bg;// Tester's right interface IPv6 address (used for sending background traffic)
  

  uint8_t mac_left_tester[6];  // Tester's left interface MAC address
  uint8_t mac_right_tester[6]; // Tester's right interface MAC address
  uint8_t mac_left_dut[6];     // DUT's left interface MAC address
  uint8_t mac_right_dut[6];    // DUT's right interface MAC address

  uint16_t fwd_dport_min; // minumum value for fg. and bg. destination port in the forward direction
  uint16_t fwd_dport_max; // maximum value for fg. and bg. destination port in the forward direction
  uint16_t rev_sport_min; // minumum value for fg. and bg. source port in the reverse direction
  uint16_t rev_sport_max; // maximum value for fg. and bg. source port in the reverse direction

  uint16_t fwd_sport_min; // minumum value for bg. destination port in the forward direction
  uint16_t fwd_sport_max; // maximum value for bg. destination port in the forward direction
  uint16_t rev_dport_min; // minumum value for bg. source port in the reverse direction
  uint16_t rev_dport_max; // maximum value for bg. source port in the reverse direction

  int cpu_left_sender; 		// lcore for left side Sender
  int cpu_right_receiver; 	// lcore for right side Receiver
  int cpu_right_sender; 	// lcore for right side Sender
  int cpu_left_receiver; 	// lcore for left side Receiver

  uint8_t memory_channels; // Number of memory channnels (for the EAL init.)
  int forward, reverse;    // directions are active if set
  int promisc;             // promiscuous mode is active if set

  // positional parameters from command line
  uint16_t ipv6_frame_size; // size of the frames carrying IPv6 datagrams (including the 4 bytes of the FCS at the end)
  uint16_t ipv4_frame_size; // redundant parameter, automatically set as ipv6_frame_size-20
  uint32_t frame_rate;      // number of frames per second
  uint16_t test_duration;   // test duration (in seconds, 1-3600)
  uint16_t stream_timeout;  // Stream timeout (in milliseconds, 0-60000)
  uint32_t n, m;            // modulo and threshold for controlling background traffic proportion

  // further data members, set by init()
  rte_mempool *pkt_pool_left_sender, *pkt_pool_right_receiver; // packet pools for the forward direction testing
  rte_mempool *pkt_pool_right_sender, *pkt_pool_left_receiver; // packet pools for the reverse direction testing
  uint64_t hz;                                                 // number of clock cycles per second
  uint64_t start_tsc;                                          // sending of the test frames will begin at this time
  uint64_t finish_receiving;                                   // receiving of the test frames will end at this time
  uint64_t frames_to_send;                                     // number of frames to send

  // lw4o6 parameters
  uint32_t psid_length;            // The number of BMR's PSID bits
  uint32_t psid;
  uint16_t num_of_port_sets;      // The number of port sets that can be obtained according to the psid_length
  uint16_t num_of_ports;          // The number of ports in each port set
  uint32_t number_of_lwB4s;             // Number of simulated lwB4s
  struct in6_addr ipv6_tunnel; // The tunnel endpoint
  lwB4_data *lwB4_array;
  std::vector<lwB4_data> tmp_lwb4data; // for reading the lwB4 data file
  int system_ports;                     //1 if lwB4s can use system ports, 0 otherwise

  // helper functions (see their description at their definition)
  int findKey(const char *line, const char *key);
  int readConfigFile(const char *filename);
  int readlwB4Data(const char *filename);
  int readCmdLine(int argc, const char *argv[]);
  int init(const char *argv0, uint16_t leftport, uint16_t rightport);
  virtual int senderPoolSize();
  void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);


  // perform throughput measurement
  void measure(uint16_t leftport, uint16_t rightport);

  Throughput();

  int generate_lwB4Data(int argc, const char *argv[]);  
};

// send test frame
int send6(void *par);
int send4(void *par);

// receive and count test frames
int receive(void *par);

// receive and count test frames
int receive(void *par);

struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, uint32_t *dst_ip);
void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type);
void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, uint32_t *dst_ip);
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length);
void mkData(uint8_t *data, uint16_t length);
void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);

struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ip, struct in6_addr *dst_ip);
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, struct in6_addr *src_ip, struct in6_addr *dst_ip, uint8_t proto);
struct rte_mbuf *mkTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6,
                              const uint32_t *src_ipv4, uint32_t *dst_ipv4);

// to store identical parameters for both senders
class senderCommonParameters
{
public:
  uint16_t ipv6_frame_size; 
  uint16_t ipv4_frame_size; 
  uint32_t frame_rate;      
  uint16_t test_duration;   
  uint32_t n, m;            
  uint64_t hz;              
  uint64_t start_tsc;       
  uint64_t frames_to_send;  
  uint32_t number_of_lwB4s;
  lwB4_data *lwB4_array;

  uint32_t *ipv4_server;
  struct in6_addr *ipv6_tunnel; //
  struct in6_addr *ipv6_left_bg; 
  struct in6_addr *ipv6_right_bg;
  
  uint16_t fwd_sport_min; 
  uint16_t fwd_sport_max;
  uint16_t fwd_dport_min; 
  uint16_t fwd_dport_max;
  uint16_t rev_sport_min; 
  uint16_t rev_sport_max;
  uint16_t rev_dport_min; 
  uint16_t rev_dport_max;

  senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                        uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
                        struct in6_addr *ipv6_tunnel_, uint32_t *ipv4_server_, in6_addr *ipv6_left_bg_, struct in6_addr *ipv6_right_bg_,
                        uint16_t fwd_sport_min_, uint16_t fwd_sport_max_, uint16_t fwd_dport_min_, uint16_t fwd_dport_max_,
                        uint16_t rev_sport_min_, uint16_t rev_sport_max_, uint16_t rev_dport_min_, uint16_t rev_dport_max_);
  senderCommonParameters();
};

class senderParameters
{
public:
  class senderCommonParameters *cp; // a pointer to the common parameters
  rte_mempool *pkt_pool; // sender's packet pool
  uint8_t eth_id; // ethernet ID
  const char *direction; // test direction (forward or reverse)
  struct ether_addr *dst_mac, *src_mac; // destination and source mac addresses
  
  //unsigned var_sport, var_dport; // how source and destination port numbers vary? 1:increase, 2:decrease, or 3:pseudorandomly change
  //suint16_t preconfigured_port_min, preconfigured_port_max; // The preconfigured range of ports (i.e., destination in case of forward and source in case of reverse)
  
  senderParameters(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                   struct ether_addr *dst_mac_, struct ether_addr *src_mac_);
  
  senderParameters();
};

// to store parameters for each receiver
class receiverParameters
{
public:
  uint64_t finish_receiving; // this one is common, but it was not worth dealing with it.
  uint8_t eth_id;
  const char *direction;

  receiverParameters(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_);
  receiverParameters();
};
#endif
