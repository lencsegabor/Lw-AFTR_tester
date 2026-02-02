#include "defines.h"
#include "includes.h"
#include "Throughput.h"
#include "pdv.h"

int hexdumpIPheader(void *h)
{
  int i; unsigned char *p;
  p=(unsigned char *)h;
  for ( i=0; i<20; i++ )
  {
    printf("%2x",(unsigned char)p[i]);
  }   
  printf("\n");
  return 0;
}

int PDV::readCmdLine(int argc, const char *argv[])
{
  if (Throughput::readCmdLine(argc - 1, argv) < 0)
    return -1;
  if (sscanf(argv[7], "%hu", &frame_timeout) != 1 || frame_timeout >= 1000 * test_duration + stream_timeout)
  {
    std::cerr << "Input Error: Frame timeout must be less than 1000*test_duration+stream_timeout, (0 means PDV measurement)." << std::endl;
    return -1;
  }
  return 0;
}

struct rte_mbuf *mkPDVTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
  const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
  const uint32_t *src_ip, uint32_t *dst_ip)
{
  // printf("inside mkTestFrame4: the beginning\n");
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf){
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    //std::cerr << "Error: %s sender can't allocate a new mbuf for the Test Frame!" << std::endl;
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
  mkUdpHeader(udp_hd, udp_length);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkPDVData(udp_data, data_length);
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

struct rte_mbuf *mkPDVTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
  const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
  struct in6_addr *src_ip, struct in6_addr *dst_ip)
{
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf){
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    //std::cerr << "Error: %s sender can't allocate a new mbuf for the Test Frame!" << std::endl;
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
  mkPDVData(udp_data, data_length);
  udp_hd->dgram_cksum = ~rte_ipv6_udptcp_cksum(ip_hdr, udp_hd); // the uncomplemented UDP checksum is calculated and set
  return pkt_mbuf;
}

struct rte_mbuf *mkPDVTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
  const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
  struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6,
  const uint32_t *src_ipv4, uint32_t *dst_ipv4)
  {
    struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
    if (!pkt_mbuf){
      rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
      //std::cerr << "Error: %s sender can't allocate a new mbuf for the Test Frame!" << std::endl;
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
    mkPDVData(udp_data, data_length);
    // udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4_hdr, udp_hd); // UDP checksum is calculated and set
    // The line above caused problem because the final step of the calculation was not reversible
    // To be able to manipulate the UDP checksum later, the uncomplemented UDP checksum is stored below:
    uint32_t cksum = rte_raw_cksum(udp_hd, udp_length);
    cksum += rte_ipv4_phdr_cksum(ipv4_hdr, 0);
    cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);
    cksum = ((cksum & 0xffff0000) >> 16) + (cksum & 0xffff);  // twice must be enough
    udp_hd->dgram_cksum = (uint16_t)cksum;    // The uncomplemented UDP checksum is stored (for further processing).
    ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr);
    // printf("DEBUG: IPv4 in IPv6 frame crated: header checksum: %x\n",ipv4_hdr->hdr_checksum); //DEBUG
    // printf("DEBUG: IPv4 in IPv6 frame header: "); //DEBUG
    // hexdumpIPheader(ipv4_hdr);
    return pkt_mbuf;
  }

void mkPDVData(uint8_t *data, uint16_t length)
{
  unsigned i;
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identification of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  *(uint64_t *)data = 0; // place for the 64-bit serial number
  data += 8;
  length -= 8;
  for (i = 0; i < length; i++)
    data[i] = i % 256;
}

int sendPDV(void *par)
{
  //std::cout << "Send STARTED on CPU core: " << rte_lcore_id() << " Using NUMA node: " << rte_socket_id() << std::endl;
  
  //  collecting input parameters:
  class senderParametersPDV *p = (class senderParametersPDV *)par;
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
  lwB4_data *lwB4_array = cp->lwB4_array;

  struct in6_addr *dut_ipv6_tunnel = cp->dut_ipv6_tunnel;
  uint32_t *tester_fw_rec_ipv4 = cp->tester_fw_rec_ipv4;
  struct in6_addr *tester_bg_send_ipv6 = cp->tester_bg_send_ipv6;
  struct in6_addr *tester_bg_rec_ipv6 = cp->tester_bg_rec_ipv6;
  uint16_t bg_fw_dport_min = p->bg_fw_dport_min; 
  uint16_t bg_fw_dport_max = p->bg_fw_dport_max; 
  uint16_t bg_fw_sport_min = p->bg_fw_sport_min; 
  uint16_t bg_fw_sport_max = p->bg_fw_sport_max;

  uint16_t bg_rv_dport_min = p->bg_rv_dport_min; 
  uint16_t bg_rv_dport_max = p->bg_rv_dport_max; 
  uint16_t bg_rv_sport_min = p->bg_rv_sport_min; 
  uint16_t bg_rv_sport_max = p->bg_rv_sport_max;

  uint16_t dport_min = cp->fw_dport_min; 
  uint16_t dport_max = cp->fw_dport_max;

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

  //for PDV
  uint64_t **send_ts = p->send_ts;
    
  // temperoray initial IP addresses that will be put in the template packets and they will be changed later in the sending loop
  // useful to calculate correct checksums 
  //(more specifically, the uncomplemented checksum start value after calculating it by the DPDK rte_ipv4_cksum(), rte_ipv4_udptcp_cksum(), and rte_ipv6_udptcp_cksum() functions
  // when creating the template packets)
  uint32_t zero_dst_ipv4, zero_src_ipv4, zero_ipv4_lwb4;
  struct in6_addr zero_src_ipv6,zero_dst_ipv6;

 // the dst_ipv4 must initially be "0.0.0.0" in order for the ipv4 header checksum to be calculated correctly by The rte_ipv4_cksum() 
 // and for the udp checksum to be calculated correctly the rte_ipv4_udptcp_cksum()
 // and consequently calculate correct checksums in the mKTestFrame4()
  if (inet_pton(AF_INET, "0.0.0.0", reinterpret_cast<void *>(&zero_dst_ipv4)) != 1)
  {
    std::cerr << "Input Error: Bad virt_dst_ipv4 address." << std::endl;
    return -1;
  }

  // The src_ipv4 will be set later from lwb4_array.
  if (inet_pton(AF_INET, "0.0.0.0", reinterpret_cast<void *>(&zero_src_ipv4)) != 1)
  {
    std::cerr << "Input Error: Bad virt_src_ipv4 address." << std::endl;
    return -1;
  }
  
  // the src_ipv6 must initially be "::" for the udp checksum to be calculated correctly by the rte_ipv6_udptcp_cksum
  // and consequently calculate correct checksum in the mKTestFrame6()
  if (inet_pton(AF_INET6, "::", reinterpret_cast<void *>(&zero_src_ipv6)) != 1)
  {
    std::cerr << "Input Error: Bad  virt_src_ipv6 address." << std::endl;
    return -1;
  }
  
  if (inet_pton(AF_INET6, "::", reinterpret_cast<void *>(&zero_dst_ipv6)) != 1)
  {
    std::cerr << "Input Error: Bad  virt_dst_ipv6 address." << std::endl;
    return -1;
  }
  
  // These addresses are for the foreground traffic in the reverse direction
  // setting the source ipv4 address of the reverse direction to the ipv4 address of the tester right interface, "the remote server"
  uint32_t *src_ipv4_rev = tester_fw_rec_ipv4;// This would be set without change during testing in the reverse direction.
                            
  //*src_ipv4_rev = htonl(*src_ipv4_rev);
  
  uint32_t *dst_ipv4_rev = &zero_dst_ipv4; // This would be variable during testing in the reverse direction.
                                       // It will represent the simulated lwB4, read from lwb4_array

  uint32_t *src_ipv4_forw = &zero_ipv4_lwb4; //This will lwB4 ipv4 address in the forward direction
  uint32_t *dst_ipv4_forw = tester_fw_rec_ipv4; //This is the "remote server" address

  // These addresses are for the foreground traffic in the forward direction
  struct in6_addr *src_ipv6_forw = &zero_src_ipv6; // This would be variable during testing in the forward direction.
                                              // It will represent the simulated lwB4 IPv6 address
                                              //and is merely specified inside the sending loop using the lwb4_array
                                               
  struct in6_addr *dst_ipv6_forw = dut_ipv6_tunnel; // This would be set without change during testing in the forward direction.
                                        // It will represent the lwAFTR IPv6 tunnel address address.
                                       
  // These addresses are for the background traffic only
  struct in6_addr *src_bg = (direction == "forward" ? tester_bg_send_ipv6 : tester_bg_rec_ipv6);  
  struct in6_addr *dst_bg = (direction == "forward" ? tester_bg_rec_ipv6 : tester_bg_send_ipv6); 
  
  uint16_t bg_sport_min, bg_sport_max, bg_dport_min, bg_dport_max; // worker port range variables
  
  // set the relevant ranges to the wide range prespecified in the configuration file (usually comply with RFC 4814)
  // the other ranges that are not set now. They will be set in the sending loop because they are based on the PSID of the
  //pseudorandomly enumerated CE
  /*
  Set port range for bg traffic based on conf file 
  */
  if (direction == "reverse")
  {
    bg_sport_min = bg_rv_sport_min;
    bg_sport_max = bg_rv_sport_max;
    bg_dport_min = bg_rv_dport_min;
    bg_dport_max = bg_rv_dport_max;
  }
  else //forward
  {
    bg_sport_min = bg_fw_sport_min;
    bg_sport_max = bg_fw_sport_max;
    bg_dport_min = bg_fw_dport_min;
    bg_dport_max = bg_fw_dport_max;
  }
  
  // check whether the lwB4 array is built or not
  if(!lwB4_array){
    std::cerr << "No lwB4 array can be accessed by the sender" << std::endl;
    return -1;
  }
    
  //rte_exit(EXIT_FAILURE,"No CE array can be accessed by the %s sender",direction);
  // prepare a NUMA local, cache line aligned array for send timestamps
  uint64_t *snd_ts = (uint64_t *)rte_malloc(0, 8 * frames_to_send, 128);
  if (!snd_ts){
    //rte_exit(EXIT_FAILURE, "Error: Receiver can't allocate memory for timestamps!\n");
    std::cerr << "Error: Receiver can't allocate memory for timestamps!" << std::endl;
    return -1;
  }
  *send_ts = snd_ts; // return the address of the array to the caller function  
  
  // implementation of varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
  // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
  // always one of the same N pre-prepared foreground or background frames is updated and sent,
  // source and/or destination IP addresses and port number(s), and UDP and are updated
  // N size arrays are used to resolve the write after send problem

  //some worker variables
  int i;                                                       // cycle variable for the above mentioned purpose: takes {0..N-1} values
  int current_lwB4;                                              // index variable to the current simulated CE in the CE_array
  uint16_t psid;                                               // working variable for the pseudorandomly enumerated PSID of the currently simulated CE
  struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
  uint8_t *pkt;                                                // working pointer to the current frame (in the message buffer)
  
  //IP workers
  uint32_t *fg_dst_ipv4[N], *fg_src_ipv4[N], *fg_dst_tun_ipv4[N], *fg_src_tun_ipv4[N];
  struct in6_addr *fg_src_ipv6[N], *fg_dst_ipv6[N];
  struct in6_addr *bg_src_ipv6[N], *bg_dst_ipv6[N];
  uint16_t *fg_ipv4_chksum[N], *fg_tun_ipv4_chksum[N];
  
  //UDP workers
  uint16_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; 
  uint16_t *udp_sport, *udp_dport, *udp_chksum;   

  uint16_t fg_udp_chksum_start, bg_udp_chksum_start, fg_ipv4_chksum_start, fg_tun_ipv4_chksum_start; // starting values (uncomplemented checksums taken from the original frames created by mKTestFrame functions)                    
  uint32_t chksum = 0; // temporary variable for UDP checksum calculation
  uint32_t ip_chksum = 0; //temporary variable for IPv4 header checksum calculation
  uint16_t sport, dport, bg_sport, bg_dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
  uint16_t sp, dp;                           // values of source and destination port numbers -- temporary values
  uint16_t tunneled_frame_size = ipv6_frame_size + 20;

  unsigned char *DEBUG_fg_tun_ipv4_hdr[N];

  //for PDV
  uint64_t *fg_counter[N], *bg_counter[N]; // pointers to the given fields
  uint64_t *counter;                       // working pointer to the counter in the currently manipulated frame

  // creating buffers of template test frames
 for (i = 0; i < N; i++)
  {
    // create a foreground Test Frame
    if (direction == "reverse")
    {
      fg_pkt_mbuf[i] = mkPDVTestFrame4(ipv4_frame_size, pkt_pool, direction, dst_mac, src_mac, src_ipv4_rev, dst_ipv4_rev);
      pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      // the source ipv4 address will not be manipulated as it will permenantly be the tester-right-ipv4 (extracted from the dmr-ipv6 as done above)
      fg_ipv4_chksum[i] = (uint16_t *)(pkt + 24);
      fg_ipv4_chksum_start = ~*fg_ipv4_chksum[i]; // save the uncomplementd checksum calculated by the rte_ipv4_cksum() in mkTestFrame4(). It is same for all (i)
      fg_dst_ipv4[i] = (uint32_t *)(pkt + 30); // The destination ipv4 should be manipulated in the sending loop as it will come from lwB4 data (i.e. changing each time) in the reverse direction
      // The source address will not be manipulated as it will permentantly be the IP address of the right interface of the Tester (as done in the initilization above)
      fg_udp_sport[i] = (uint16_t *)(pkt + 34); //?? Can it be constant
      fg_udp_dport[i] = (uint16_t *)(pkt + 36); // Need to change based on lwB4 data
      fg_udp_chksum[i] = (uint16_t *)(pkt + 40);
      fg_counter[i] = (uint64_t *)(pkt + 50);
    }
    else
    { //"forward"
      fg_pkt_mbuf[i] = mkPDVTestIpv4inIpv6Tun(tunneled_frame_size,pkt_pool,direction,dst_mac,src_mac, src_ipv6_forw, dst_ipv6_forw, src_ipv4_forw, dst_ipv4_forw);
      pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *);
      fg_src_ipv6[i] = (struct in6_addr *)(pkt + 22);    // The source address should be manipulated as it will be the right address (i.e. changing each time) in the forward direction
      fg_tun_ipv4_chksum[i] = (uint16_t *)(pkt + 64);
      fg_tun_ipv4_chksum_start = ~*fg_tun_ipv4_chksum[i];
      fg_src_tun_ipv4[i] = (uint32_t *)(pkt + 66);
      fg_udp_sport[i] = (uint16_t *)(pkt + 74);
      fg_udp_dport[i] = (uint16_t *)(pkt + 76);
      fg_udp_chksum[i] = (uint16_t *)(pkt + 80);
      fg_counter[i] = (uint64_t *)(pkt + 90);
      DEBUG_fg_tun_ipv4_hdr[i] = (unsigned char *)(pkt + 54);
    }
    fg_udp_chksum_start = *fg_udp_chksum[i]; // save the uncomplemented UDP checksum value (same for all values of "i")

    // Always create a backround Test Frame (it is always an IPv6 frame) regardless of the direction of the test
    // The source and destination IP addresses of the packet have already been set in the initialization above
    // and they will permenantely be the IP addresses of the left and right interfaces of the Tester 
    // and based on the direction of the test 
    bg_pkt_mbuf[i] = mkPDVTestFrame6(ipv6_frame_size, pkt_pool, direction, dst_mac, src_mac, src_bg, dst_bg);
    pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    bg_udp_sport[i] = (uint16_t *)(pkt + 54);
    bg_udp_dport[i] = (uint16_t *)(pkt + 56);
    bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
    bg_counter[i] = (uint64_t *)(pkt + 70);
  }
  
  //save the uncomplemented UDP checksum value (same for all values of [i]). So, [0] is enough
  fg_udp_chksum_start = *fg_udp_chksum[0]; // for the foreground frames
  bg_udp_chksum_start = *bg_udp_chksum[0]; // same but for the background frames
  
  // save the uncomplemented IPv4 header checksum (same for all values of [i]). So, [0] is enough
  if (direction == "reverse") // in case of foreground IPv4 only
      fg_ipv4_chksum_start = ~*fg_ipv4_chksum[0]; 
  
  if (direction == "forward")
      fg_tun_ipv4_chksum_start = ~*fg_tun_ipv4_chksum[0]; 
  
  
  i = 0; // increase maunally after each sending
  current_lwB4 = 0; // increase maunally after each sending

  // prepare random number infrastructure
  thread_local std::random_device rd_sport;           // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_sport(rd_sport()); // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::random_device rd_dport;           // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_dport(rd_dport()); // Standard 64-bit mersenne_twister_engine seeded with rd()

  // naive sender version: it is simple and fast
  for (sent_frames = 0; sent_frames < frames_to_send; sent_frames++)
  { // Main cycle for the number of frames to send
    bool IsUDPoverIPv4;         // It is true for foreground frames, and false for background frames.
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
      counter = fg_counter[i];

      if (direction == "forward")
      {
        //Set the IPv4 packet fields, IP addresses, checksum
        ip_chksum = fg_tun_ipv4_chksum_start; // restore the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
        // printf("DEBUG: IPv4 in IPv6 frame to be sent: starting header checksum: %x\n", ip_chksum); //DEBUG
        // printf("DEBUG: IPv4 in IPv6 frame header at start: "); //DEBUG
        // hexdumpIPheader(DEBUG_fg_tun_ipv4_hdr[i]);
        *fg_src_tun_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; //set it with the CE's IPv4 address
        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *fg_tun_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet
    
        // printf("DEBUG: IPv4 in IPv6 frame to be sent: set header checksum: %x\n", *fg_tun_ipv4_chksum[i]); //DEBUG
        // printf("DEBUG: IPv4 in IPv6 frame header when ready: "); //DEBUG
        // hexdumpIPheader(DEBUG_fg_tun_ipv4_hdr[i]);
        
        *fg_src_ipv6[i] = lwB4_array[current_lwB4].b4_ipv6_addr; // set it with the right address

        std::uniform_int_distribution<int> uni_dis_sport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port); // uniform distribution in [sport_min, sport_max]
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum

        std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max); // uniform distribution in [sport_min, sport_max]
        sp = uni_dis_dport(gen_sport);
        *udp_dport = htons(sp); // set the source port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }

      if (direction == "reverse")
      {
        ip_chksum = fg_ipv4_chksum_start; // restore the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address

        *fg_dst_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; //set it with the LwB4's IPv4 address

        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *fg_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

        // the dport_min and dport_max will be set according to the port range values of the selected port set and the dport will retrieve its last value within this range
        // the sport_min and sport_max will remain on thier default values within the wide range. The sport will be changed based on its value from the last cycle.
        std::uniform_int_distribution<int> uni_dis_dport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port); // uniform distribution in [sport_min, sport_max]
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum

        std::uniform_int_distribution<int> uni_dis_sport(dport_min, dport_max); // uniform distribution in [sport_min, sport_max]
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
      }
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
      counter = bg_counter[i];
      // time to change the value of the source and destination port numbers
   
      // pseudorandom port numbers
      std::uniform_int_distribution<int> uni_dis_sport(bg_sport_min, bg_sport_max); // uniform distribution in [bg_fw_sport_min, bg_fw_sport_max]
      sp = uni_dis_sport(gen_sport);
      
      *udp_sport = htons(sp); // set the source port 
      chksum += *udp_sport; // and add it to the UDP checksum
   
      // pseudorandom port numbers
      std::uniform_int_distribution<int> uni_dis_dport(bg_dport_min, bg_dport_max); // uniform distribution in [bg_fw_dport_min, bg_fw_dport_max]
      dp = uni_dis_dport(gen_dport);
      
      *udp_dport = htons(dp); // set the destination port 
      chksum += *udp_dport; // and add it to the UDP checksum
    
    }

    *counter = sent_frames;
    chksum += rte_raw_cksum(&sent_frames, 8);

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

    snd_ts[sent_frames] = rte_rdtsc(); // store timestamp
    current_lwB4 = (current_lwB4 + 1) % num_of_lwB4s; // proceed to the next CE element in the CE array
    i = (i + 1) % N;
  } // this is the end of the sending cycle

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc() - start_tsc) / hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", direction, elapsed_seconds);
  if (elapsed_seconds > test_duration * TOLERANCE){
    //rte_exit(EXIT_FAILURE, "%s sending exceeded the %3.10lf seconds limit, the test is invalid.\n", direction, test_duration * TOLERANCE);
    std::cout << direction << " sending exceeded the " << test_duration * TOLERANCE << " seconds limit, the test is invalid." << std::endl;
    return -1;
  }
  printf("%s frames sent: %lu\n", direction, sent_frames);  

  return 0;
}

int receivePDV(void *par)
{
  //std::cout << "Receive STARTED on CPU core: " << rte_lcore_id() << " Using NUMA node: " << rte_socket_id() << std::endl;
  
  // collecting input parameters:
  class receiverParametersPDV *p = (class receiverParametersPDV *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;
  uint64_t num_frames = p->num_frames;
  uint16_t frame_timeout = p->frame_timeout;
  uint64_t **receive_ts = p->receive_ts;

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST];                      // pointers for the mbufs of received frames
  uint16_t ipv4 = htons(0x0800);                                  // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6 = htons(0x86DD);                                  // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identificion of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  uint64_t received = 0; // number of received frames

  //for PDV
  uint64_t timestamp, counter;

  // prepare a NUMA local, cache line aligned array for reveive timestamps, and fill it with all 0-s
  uint64_t *rec_ts = (uint64_t *)rte_zmalloc(0, 8 * num_frames, 128);
  if (!rec_ts){
    //rte_exit(EXIT_FAILURE, "Error: Receiver can't allocate memory for timestamps!\n");
    std::cerr << "Error: Receiver can't allocate memory for timestamps!" << std::endl;
    return -1;
  }
  *receive_ts = rec_ts; // return the address of the array to the caller function

  while (rte_rdtsc() < finish_receiving)
  {
    
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);

    for (i = 0; i < frames; i++)
    { 
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message bufferq

      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if (*(uint16_t *)&pkt[12] == ipv6)
      { /* IPv4 in IPv6 */
        /* check if IPv6 Next Header is IPIP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if (likely(pkt[20] == 4 && *(uint64_t *)&pkt[82] == *id)){
          // PDV frame
          timestamp = rte_rdtsc(); // get a timestamp ASAP
          counter = *(uint64_t *)&pkt[90];
          if (unlikely(counter >= num_frames))
          {
            //rte_exit(EXIT_FAILURE, "Error: IPV4inIPv6 PDV Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
            std::cout << "Error: IPV4inIPv6 PDV Frame with invalid frame ID was received!" << std::endl;
            return -1;
          }
          rec_ts[counter] = timestamp;
          received++; // also count it
        }else if (likely(pkt[20] == 17 && *(uint64_t *)&pkt[62] == *id))
        {
          // PDV frame
          timestamp = rte_rdtsc(); // get a timestamp ASAP
          counter = *(uint64_t *)&pkt[70];
          if (unlikely(counter >= num_frames))
            //rte_exit(EXIT_FAILURE, "Error: PDV Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
            std::cout << "Error: IPv4 PDV Frame with invalid frame ID was received!" << std::endl;
          rec_ts[counter] = timestamp;
          received++; // also count it
        }
      }
      else if (*(uint16_t *)&pkt[12] == ipv4)
      { /* IPv4 */
         /* check if IPv4 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if (likely(pkt[23] == 17 && *(uint64_t *)&pkt[42] == *id))
          // Latency Frame
          timestamp = rte_rdtsc(); // get a timestamp ASAP
          counter = *(uint64_t *)&pkt[50];
          if (unlikely(counter >= num_frames))
          {
            //rte_exit(EXIT_FAILURE, "Error: IPv4 PDV Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
            std::cout << "Error: IPv4 PDV Frame with invalid frame ID was received!" << std::endl;
            return -1;
          }
          rec_ts[counter] = timestamp;
          received++; // also count it
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  if (frame_timeout == 0)
    printf("%s frames received: %lu\n", direction, received);
  return received;
}

void PDV::measure(uint16_t leftport, uint16_t rightport)
{
  //std::cout << "measure runs on CPU core: " << rte_lcore_id() << std::endl;

  senderCommonParameters scp,scp2;
  senderParametersPDV spars, spars2;
  receiverParametersPDV rpars, rpars2;

  uint64_t *left_send_ts, *right_send_ts, *left_receive_ts, *right_receive_ts; // pointers for timestamp arrays
  
  if (forward)
  { // Left to right direction is active
    
    scp = senderCommonParameters(ipv6_frame_size, ipv4_frame_size, frame_rate, test_duration,
      n, m, hz, start_tsc, number_of_lwB4s, lwB4_array, &dut_ipv6_tunnel, &tester_fw_rec_ipv4,
      &tester_bg_send_ipv6, &tester_bg_rec_ipv6, fwd_dport_min, fwd_dport_max
      );
    // set individual parameters for the left sender
    // Initialize the parameter class instance

    spars = senderParametersPDV(&scp, pkt_pool_left_sender, leftport, "forward", (ether_addr *)dut_fw_mac, (ether_addr *)tester_fw_mac, bg_fw_sport_min, bg_fw_sport_max,
                          bg_fw_dport_min, bg_fw_dport_max, bg_rv_sport_min, bg_rv_sport_max, bg_rv_dport_min, bg_rv_dport_max, &left_send_ts);

    // start left sender
    if (rte_eal_remote_launch(sendPDV, &spars, cpu_fw_send))
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    rpars = receiverParametersPDV(finish_receiving, rightport, "forward", test_duration * frame_rate, frame_timeout, &right_receive_ts);

    // start right receiver
    if (rte_eal_remote_launch(receivePDV, &rpars, cpu_fw_receive))
      std::cout << "Error: could not start Right Receiver." << std::endl;
  }

  if (reverse) 
  {
    // Right to Left direction is active
    scp2 = senderCommonParameters(ipv6_frame_size, ipv4_frame_size, frame_rate, test_duration,
      n, m, hz, start_tsc, number_of_lwB4s, lwB4_array, &dut_ipv6_tunnel, &tester_fw_rec_ipv4,
      &tester_bg_send_ipv6, &tester_bg_rec_ipv6, fwd_dport_min, fwd_dport_max
      );
    // set individual parameters for the right sender
    // Initialize the parameter class instance
    spars2 = senderParametersPDV(&scp2, pkt_pool_right_sender, rightport, "reverse", (ether_addr *)dut_rv_mac, (ether_addr *)tester_rv_mac, bg_fw_sport_min, bg_fw_sport_max,
                          bg_fw_dport_min, bg_fw_dport_max, bg_rv_sport_min, bg_rv_sport_max, bg_rv_dport_min, bg_rv_dport_max, &right_send_ts);

    // start right sender
    if (rte_eal_remote_launch(sendPDV, &spars2, cpu_rv_send))
      std::cout << "Error: could not start Right Sender." << std::endl;
    
    // set parameters for the left receiver
    rpars2 = receiverParametersPDV(finish_receiving, leftport, "reverse", test_duration * frame_rate, frame_timeout, &left_receive_ts);

    // start left receiver
    if (rte_eal_remote_launch(receivePDV, &rpars2, cpu_rv_receive))
      std::cout << "Error: could not start Left Receiver." << std::endl; 
  }
  
  std::cout << "Info: Testing started." << std::endl;

  // wait until active senders and receivers finish
  if (forward)
  {
    rte_eal_wait_lcore(cpu_fw_send);
    rte_eal_wait_lcore(cpu_fw_receive);
  }
  if (reverse)
  {
    rte_eal_wait_lcore(cpu_rv_send);
    rte_eal_wait_lcore(cpu_rv_receive);
  }

  // Process the timestamps
  int penalty = 1000 * test_duration + stream_timeout; // latency to be reported for lost timestamps, expressed in milliseconds

  if (forward)
    evaluatePDV(test_duration * frame_rate, left_send_ts, right_receive_ts, hz, frame_timeout, penalty, "forward");
  if (reverse)
    evaluatePDV(test_duration * frame_rate, right_send_ts, left_receive_ts, hz, frame_timeout, penalty, "reverse");

  rte_free(lwB4_array); // release the lwB4 data memory

  std::cout << "Info: Test finished." << std::endl;
}

senderParametersPDV::senderParametersPDV(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
  struct ether_addr *dst_mac_, struct ether_addr *src_mac_, uint16_t bg_fw_sport_min_, uint16_t bg_fw_sport_max_, uint16_t bg_fw_dport_min_,
  uint16_t bg_fw_dport_max_, uint16_t bg_rv_sport_min_, uint16_t bg_rv_sport_max_, uint16_t bg_rv_dport_min_, uint16_t bg_rv_dport_max_, uint64_t **send_ts_) : senderParameters(cp_, pkt_pool_, eth_id_, direction_, dst_mac_, src_mac_, bg_fw_sport_min_, bg_fw_sport_max_, bg_fw_dport_min_, bg_fw_dport_max_,
  bg_rv_sport_min_, bg_rv_sport_max_, bg_rv_dport_min_, bg_rv_dport_max_)
  {
    send_ts = send_ts_;
  }

senderParametersPDV::senderParametersPDV(){}  

receiverParametersPDV::receiverParametersPDV(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_,
  uint64_t num_frames_, uint16_t frame_timeout_, uint64_t **receive_ts_) : receiverParameters(finish_receiving_, eth_id_, direction_)
  {
    num_frames = num_frames_;
    frame_timeout = frame_timeout_;
    receive_ts = receive_ts_;
  }

receiverParametersPDV::receiverParametersPDV(){}

void evaluatePDV(uint64_t num_of_frames, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, uint16_t frame_timeout, int penalty, const char *direction)
{
  int64_t frame_to = frame_timeout * hz / 1000;  // exchange frame timeout from ms to TSC
  int64_t penalty_tsc = penalty * hz / 1000;     // exchange penaly from ms to TSC
  int64_t PDV, Dmin, D99_9th_perc, Dmax;         // signed variable are used to prevent [-Wsign-compare] warning :-)
  uint64_t i;                                    // cycle variable
  int64_t *latency = new int64_t[num_of_frames]; // negative delay may occur, see the paper for details
  uint64_t num_corrected = 0;                    // number of negative delay values corrected to 0
  uint64_t frames_lost = 0;                      // the number of physically lost frames

  if (!latency){
    rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency values!\n");
    //std::cerr << "Error: Tester can't allocate memory for latency values!" << std::endl;
    //return -1;
  }

  for (i = 0; i < num_of_frames; i++)
  {
    if (receive_ts[i])
    {
      latency[i] = receive_ts[i] - send_ts[i]; // packet delay in TSC
      if (unlikely(latency[i] < 0))
      {
        latency[i] = 0; // correct negative delay to 0
        num_corrected++;
      }
    }
    else
    {
      frames_lost++;            // frame physically lost
      latency[i] = penalty_tsc; // penalty of the lost timestamp
    }
  }
  if (num_corrected)
    printf("Debug: %s number of negative delay values corrected to 0: %lu\n", direction, num_corrected);
  if (frame_timeout)
  {
    // count the frames arrived in time
    uint64_t frames_received = 0;
    for (i = 0; i < num_of_frames; i++)
      if (latency[i] <= frame_to)
        frames_received++;
    printf("%s frames received: %lu\n", direction, frames_received);
    printf("Info: %s frames completely missing: %lu\n", direction, frames_lost);
  }
  else
  {
    // calculate PDV
    // first, find Dmin
    Dmin = Dmax = latency[0];
    for (i = 1; i < num_of_frames; i++)
    {
      if (latency[i] < Dmin)
        Dmin = latency[i];
      if (latency[i] > Dmax)
        Dmax = latency[i];
      if (latency[i] > penalty_tsc)
        printf("Debug: BUG: i=%lu, send_ts[i]=%lu, receive_ts[i]=%lu, latency[i]=%lu\n", i, send_ts[i], receive_ts[i], latency[i]);
    }
    // then D99_9th_perc
    std::sort(latency, latency + num_of_frames);
    D99_9th_perc = latency[int(ceil(0.999 * num_of_frames)) - 1];
    PDV = D99_9th_perc - Dmin;

    // print the results
    printf("Info: %s D99_9th_perc: %lf\n", direction, 1000.0 * D99_9th_perc / hz);
    printf("Info: %s Dmin: %lf\n", direction, 1000.0 * Dmin / hz);
    printf("Info: %s Dmax: %lf\n", direction, 1000.0 * Dmax / hz);
    printf("%s PDV: %lf\n", direction, 1000.0 * PDV / hz);
  }
}
