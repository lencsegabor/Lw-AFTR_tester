#include "defines.h"
#include "includes.h"
#include "Throughput.h"
#include "latency.h"

// after reading the parameters for throughput measurement, further two parameters are read
int Latency::readCmdLine(int argc, const char *argv[])
{
  if (Throughput::readCmdLine(argc - 2, argv) < 0)
    return -1;
  if (sscanf(argv[7], "%hu", &first_tagged_delay) != 1 || first_tagged_delay > 3600)
  {
    std::cerr << "Input Error: Delay before timestamps must be between 0 and 3600." << std::endl;
    return -1;
  }
  if (test_duration <= first_tagged_delay)
  {
    std::cerr << "Input Error: Test test_duration MUST be longer than the delay before the first tagged frame." << std::endl;
    return -1;
  }
  if (sscanf(argv[8], "%hu", &num_of_tagged) != 1 || num_of_tagged < 1 || num_of_tagged > 50000)
  {
    std::cerr << "Input Error: Number of tagged frames must be between 1 and 50000." << std::endl;
    return -1;
  }
  if ((test_duration - first_tagged_delay) * frame_rate < num_of_tagged)
  {
    std::cerr << "Input Error: There are not enough test frames in the (test_duration-first_tagged_delay) interval to be tagged." << std::endl;
    return -1;
  }
  return 0;
}

int Latency::senderPoolSize()
{
  return Throughput::senderPoolSize() + num_of_tagged; // tagged frames are also pre-generated
}

int send6Latency(void *par)
{
  //  collecting input parameters:
  class senderParametersLatency *p = (class senderParametersLatency *)par;
  class senderCommonParametersLatency *cp = (class senderCommonParametersLatency *)p->cp;

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

  // parameters directly correspond to the data members of class Latency
  uint16_t first_tagged_delay = cp->first_tagged_delay;
  uint16_t num_of_tagged = cp->num_of_tagged;

  uint64_t *send_ts = p->send_ts;

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

  int latency_test_time = test_duration - first_tagged_delay;                   // lenght of the time interval, while latency frames are sent
  uint64_t frames_to_send_during_latency_test = latency_test_time * frame_rate; // precalcalculated value to speed up calculation in the loop

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
  
  // check whether the CE array is built or not
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

  //std::cout <<"NUM OF TAGGED: " <<num_of_tagged <<std::endl;
  //same for latency frames
  struct rte_mbuf *lat_fg_pkt_mbuf[num_of_tagged], *lat_bg_pkt_mbuf[num_of_tagged];
  uint32_t *lat_fg_dst_ipv4[num_of_tagged], *lat_fg_dst_tun_ipv4[N], *lat_fg_src_tun_ipv4[num_of_tagged];
  struct in6_addr *lat_fg_src_ipv6[num_of_tagged], *lat_fg_dst_ipv6[num_of_tagged];
  struct in6_addr *lat_bg_src_ipv6[num_of_tagged], *lat_bg_dst_ipv6[num_of_tagged];
  uint16_t *lat_fg_udp_sport[num_of_tagged], *lat_fg_udp_dport[num_of_tagged], *lat_fg_udp_chksum[num_of_tagged], *lat_bg_udp_sport[num_of_tagged], *lat_bg_udp_dport[num_of_tagged], *lat_bg_udp_chksum[num_of_tagged]; // pointers to the given fields
  uint16_t *lat_fg_ipv4_chksum[num_of_tagged], *lat_fg_tun_ipv4_chksum[num_of_tagged];
  uint16_t lat_fg_ipv4_chksum_start, lat_fg_tun_ipv4_chksum_start; // starting values (uncomplemented IPv4 header checksum taken from the original frames)
  
  //IMPORTANT NOTE:
  //In the latency test, there are no lat_fg_udp_chksum_start and lat_bg_udp_chksum_start as there in the throughput test becasue here every frame will have different checksum start due to its ordinal number added to its data field
 
  // creating buffers of template test frames
  for (i = 0; i < N; i++)
  {
    // create a foreground Test Frame
    fg_pkt_mbuf[i] = mkTestIpv4inIpv6Tun(tunneled_frame_size,pkt_pool,direction,dst_mac,src_mac,&zero_ipv6,ipv6_tunnel,&zero_ipv4,ipv4_server);
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
  
  // create Latency Test Frames (may be foreground frames and background frames as well)
  struct rte_mbuf **latency_frames = new struct rte_mbuf *[num_of_tagged];
  if (!latency_frames){
    return -1;
  }

  uint64_t start_latency_frame = first_tagged_delay * frame_rate; // the ordinal number of the very first latency frame

  for (int i = 0; i < num_of_tagged; i++){
    if ((start_latency_frame + i * frame_rate * latency_test_time / num_of_tagged) % n < m)
    {
      // create a foreground Latency Frame
      latency_frames[i] = mkLatencyTestIpv4inIpv6Tun(tunneled_frame_size,pkt_pool,direction,dst_mac,src_mac,&zero_ipv6,ipv6_tunnel,&zero_ipv4,ipv4_server,i);
      pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *);
      lat_fg_src_ipv6[i] = (struct in6_addr *)(pkt + 22); 
      lat_fg_tun_ipv4_chksum[i] = (uint16_t *)(pkt + 64);
      lat_fg_tun_ipv4_chksum_start = ~*lat_fg_tun_ipv4_chksum[i];
      lat_fg_src_tun_ipv4[i] = (uint32_t *)(pkt + 66);
      lat_fg_udp_sport[i] = (uint16_t *)(pkt + 74);
      lat_fg_udp_dport[i] = (uint16_t *)(pkt + 76);
      lat_fg_udp_chksum[i] = (uint16_t *)(pkt + 80);
    }
    else
    {
      // create a background Latency Frame 
      latency_frames[i] = mkLatencyTestFrame6(ipv6_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv6_left_bg,ipv6_right_bg,i);
      pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
      lat_bg_udp_sport[i] = (uint16_t *)(pkt + 54);
      lat_bg_udp_dport[i] = (uint16_t *)(pkt + 56);
      lat_bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
    }
  }  
  
  i = 0; // increase maunally after each sending
  current_lwB4 = 0; // increase maunally after each sending

  int latency_timestamp_no = 0;                           // counter for the latency frames from 0 to num_of_tagged-1
  uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame

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
    if ( unlikely(sent_frames == send_next_latency_frame) )
    {
      // a latency frame is to be sent
      if ( IsUDPoverIPv4 = sent_frames % n < m )
      {
        // foreground frame is to be sent
        psid = lwB4_array[current_lwB4].psid;
        chksum = (uint16_t) *lat_fg_udp_chksum[latency_timestamp_no];; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = lat_fg_udp_sport[latency_timestamp_no];
        udp_dport = lat_fg_udp_dport[latency_timestamp_no];
        udp_chksum = lat_fg_udp_chksum[latency_timestamp_no];
        pkt_mbuf = latency_frames[latency_timestamp_no];

        //Set the IPv4 packet fields, source IPv4 addresses, checksum
        ip_chksum = lat_fg_tun_ipv4_chksum_start; // read the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
        *lat_fg_src_tun_ipv4[latency_timestamp_no] = lwB4_array[current_lwB4].ipv4_addr; // set it with the lwB4's IPv4 address
        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;                             // add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;                          // and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *lat_fg_tun_ipv4_chksum[latency_timestamp_no] = (uint16_t)ip_chksum; // now set the IPv4 header checksum of the packet
        
        *lat_fg_src_ipv6[latency_timestamp_no] = lwB4_array[current_lwB4].b4_ipv6_addr;

        std::uniform_int_distribution<int> uni_dis_sport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port); 
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum

        std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max); 
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the source port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
      else
      {
        // background frame is to be sent
        // from here, we need to handle the background frame identified by the temporary variables
        chksum = (uint16_t) *lat_bg_udp_chksum[latency_timestamp_no];; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = lat_bg_udp_sport[latency_timestamp_no];
        udp_dport = lat_bg_udp_dport[latency_timestamp_no];
        udp_chksum = lat_bg_udp_chksum[latency_timestamp_no];
        pkt_mbuf = latency_frames[latency_timestamp_no];
    
        std::uniform_int_distribution<int> uni_dis_sport(fwd_sport_min,fwd_sport_max);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
    
        std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
    }    
    else 
    {  
      // a normal Test Frame is to be sent
      if ( IsUDPoverIPv4 = sent_frames % n < m )
      {
        // foreground frame is to be sent
        psid = lwB4_array[current_lwB4].psid;
        chksum = fg_udp_chksum_start; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = fg_udp_sport[i];
        udp_dport = fg_udp_dport[i];
        udp_chksum = fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];

        //Set the IPv4 packet fields, IP addresses, checksum
        ip_chksum = fg_tun_ipv4_chksum_start; // read the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
        *fg_src_tun_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; //set it with the lwB4's IPv4 address
        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *fg_tun_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet
        
        *fg_src_ipv6[i] = lwB4_array[current_lwB4].b4_ipv6_addr; 

        std::uniform_int_distribution<int> uni_dis_sport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum

        std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the source port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
      else
      {
        // background frame is to be sent
        // from here, we need to handle the background frame identified by the temporary variables
        chksum = bg_udp_chksum_start; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = bg_udp_sport[i];
        udp_dport = bg_udp_dport[i];
        udp_chksum = bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
    
        std::uniform_int_distribution<int> uni_dis_sport(fwd_sport_min,fwd_sport_max);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
    
        std::uniform_int_distribution<int> uni_dis_dport(fwd_dport_min,fwd_dport_max);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
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

    if ( unlikely(sent_frames == send_next_latency_frame) )
    {
      // the sent frame was a Latency Frame
      send_ts[latency_timestamp_no++] = rte_rdtsc(); // store its sending timestamp
      send_next_latency_frame = start_latency_frame + latency_timestamp_no * frames_to_send_during_latency_test / num_of_tagged; //prepare the index of the next latency frame
    }
    else
    {
      // the sent frame was a normal Test Frame
      i = (i + 1) % N;
    }

    current_lwB4 = (current_lwB4 + 1) % num_of_lwB4s; // proceed to the next CE element in the CE array
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


int send4Latency(void *par)
{
  //  collecting input parameters:
  class senderParametersLatency *p = (class senderParametersLatency *)par;
  class senderCommonParametersLatency *cp = (class senderCommonParametersLatency *)p->cp;

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

  // parameters directly correspond to the data members of class Latency
  uint16_t first_tagged_delay = cp->first_tagged_delay;
  uint16_t num_of_tagged = cp->num_of_tagged;

  uint64_t *send_ts = p->send_ts;

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

  int latency_test_time = test_duration - first_tagged_delay;                   // lenght of the time interval, while latency frames are sent
  uint64_t frames_to_send_during_latency_test = latency_test_time * frame_rate; // precalcalculated value to speed up calculation in the loop

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
  
  // check whether the CE array is built or not
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
  uint16_t tunneled_frame_size = ipv6_frame_size + 20;

  //std::cout <<"NUM OF TAGGED: " <<num_of_tagged <<std::endl;
  //same for latency frames
  struct rte_mbuf *lat_fg_pkt_mbuf[num_of_tagged], *lat_bg_pkt_mbuf[num_of_tagged];
  uint32_t *lat_fg_dst_ipv4[num_of_tagged], *lat_fg_dst_tun_ipv4[N], *lat_fg_src_tun_ipv4[num_of_tagged];
  struct in6_addr *lat_fg_src_ipv6[num_of_tagged], *lat_fg_dst_ipv6[num_of_tagged];
  struct in6_addr *lat_bg_src_ipv6[num_of_tagged], *lat_bg_dst_ipv6[num_of_tagged];
  uint16_t *lat_fg_udp_sport[num_of_tagged], *lat_fg_udp_dport[num_of_tagged], *lat_fg_udp_chksum[num_of_tagged], *lat_bg_udp_sport[num_of_tagged], *lat_bg_udp_dport[num_of_tagged], *lat_bg_udp_chksum[num_of_tagged]; // pointers to the given fields
  uint16_t *lat_fg_ipv4_chksum[num_of_tagged], *lat_fg_tun_ipv4_chksum[num_of_tagged];
  uint16_t lat_fg_ipv4_chksum_start, lat_fg_tun_ipv4_chksum_start; // starting values (uncomplemented IPv4 header checksum taken from the original frames)
  
  //IMPORTANT NOTE:
  //In the latency test, there are no lat_fg_udp_chksum_start and lat_bg_udp_chksum_start as there in the throughput test becasue here every frame will have different checksum start due to its ordinal number added to its data field
 
  // creating buffers of template test frames
  for (i = 0; i < N; i++)
  {
    // create a foreground Test Frame
    fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv4_server,&zero_ipv4);
    pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    fg_ipv4_chksum[i] = (uint16_t *)(pkt + 24);
    fg_ipv4_chksum_start = ~*fg_ipv4_chksum[i];
    fg_dst_ipv4[i] = (uint32_t *)(pkt + 30);
    fg_udp_sport[i] = (uint16_t *)(pkt + 34);
    fg_udp_dport[i] = (uint16_t *)(pkt + 36);
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

  fg_ipv4_chksum_start = ~*fg_ipv4_chksum[0]; // save the uncomplemented IPv4 header checksum

  // create Latency Test Frames (may be foreground frames and background frames as well)
  struct rte_mbuf **latency_frames = new struct rte_mbuf *[num_of_tagged];
  if (!latency_frames){
    return -1;
  }

  uint64_t start_latency_frame = first_tagged_delay * frame_rate; // the ordinal number of the very first latency frame

  for (int i = 0; i < num_of_tagged; i++){
    if ((start_latency_frame + i * frame_rate * latency_test_time / num_of_tagged) % n < m)
    {
      // create a foreground Latency Frame
      latency_frames[i] = mkLatencyTestFrame4(ipv4_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv4_server,&zero_ipv4,i);
      pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
      lat_fg_ipv4_chksum[i] = (uint16_t *)(pkt + 24);
      lat_fg_ipv4_chksum_start = ~*lat_fg_ipv4_chksum[i];
      lat_fg_dst_ipv4[i] = (uint32_t *)(pkt + 30);
      lat_fg_udp_sport[i] = (uint16_t *)(pkt + 34);
      lat_fg_udp_dport[i] = (uint16_t *)(pkt + 36);
      lat_fg_udp_chksum[i] = (uint16_t *)(pkt + 40);
    }
    else
    {
      // create a background Latency Frame
      latency_frames[i] = mkLatencyTestFrame6(ipv6_frame_size,pkt_pool,direction,dst_mac,src_mac,ipv6_right_bg,ipv6_left_bg,i);
      pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
      lat_bg_udp_sport[i] = (uint16_t *)(pkt + 54);
      lat_bg_udp_dport[i] = (uint16_t *)(pkt + 56);
      lat_bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
    }
  }  
  
  i = 0; // increase maunally after each sending
  current_lwB4 = 0; // increase maunally after each sending

  int latency_timestamp_no = 0;                           // counter for the latency frames from 0 to num_of_tagged-1
  uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame

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
    if ( unlikely(sent_frames == send_next_latency_frame) )
    {
      // a latency frame is to be sent
      if ( IsUDPoverIPv4 = sent_frames % n < m )
      {
        // foreground frame is to be sent
        psid = lwB4_array[current_lwB4].psid;
        chksum = (uint16_t) *lat_fg_udp_chksum[latency_timestamp_no];; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = lat_fg_udp_sport[latency_timestamp_no];
        udp_dport = lat_fg_udp_dport[latency_timestamp_no];
        udp_chksum = lat_fg_udp_chksum[latency_timestamp_no];
        pkt_mbuf = latency_frames[latency_timestamp_no];

        //Set the IPv4 packet fields, destination IPv4 addresses, checksum
        ip_chksum = lat_fg_ipv4_chksum_start; // read the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
        *lat_fg_dst_ipv4[latency_timestamp_no] = lwB4_array[current_lwB4].ipv4_addr; // set it with the lwB4's IPv4 address
        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;                         // add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum;                      // and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *lat_fg_ipv4_chksum[latency_timestamp_no] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

        std::uniform_int_distribution<int> uni_dis_dport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum

        std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max); 
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
      }
      else
      {
        // background frame is to be sent
        // from here, we need to handle the background frame identified by the temporary variables
        chksum = (uint16_t) *lat_bg_udp_chksum[latency_timestamp_no];; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = lat_bg_udp_sport[latency_timestamp_no];
        udp_dport = lat_bg_udp_dport[latency_timestamp_no];
        udp_chksum = lat_bg_udp_chksum[latency_timestamp_no];
        pkt_mbuf = latency_frames[latency_timestamp_no];
    
        std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
    
        std::uniform_int_distribution<int> uni_dis_dport(rev_dport_min,rev_dport_max);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
    }    
    else 
    {  
      // a normal Test Frame is to be sent
      if ( IsUDPoverIPv4 = sent_frames % n < m )
      {
        // foreground frame is to be sent
        psid = lwB4_array[current_lwB4].psid;
        chksum = fg_udp_chksum_start; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = fg_udp_sport[i];
        udp_dport = fg_udp_dport[i];
        udp_chksum = fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];

        //Set the IPv4 packet fields, IP addresses, checksum
        ip_chksum = fg_ipv4_chksum_start; // read the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address
        *fg_dst_ipv4[i] = lwB4_array[current_lwB4].ipv4_addr; //set it with the lwB4's IPv4 address
        chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //add its chechsum to the UDP checksum
        ip_chksum += lwB4_array[current_lwB4].ipv4_addr_chksum; //and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        *fg_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

        std::uniform_int_distribution<int> uni_dis_dport(lwB4_array[current_lwB4].min_port, lwB4_array[current_lwB4].max_port);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum
    
        std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
      }
      else
      {
        // background frame is to be sent
        // from here, we need to handle the background frame identified by the temporary variables
        chksum = bg_udp_chksum_start; // read the uncomplemented UDP checksum to add the values of the varying fields
        udp_sport = bg_udp_sport[i];
        udp_dport = bg_udp_dport[i];
        udp_chksum = bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
    
        std::uniform_int_distribution<int> uni_dis_sport(rev_sport_min,rev_sport_max);
        sp = uni_dis_sport(gen_sport);
        *udp_sport = htons(sp); // set the source port 
        chksum += *udp_sport; // and add it to the UDP checksum
    
        std::uniform_int_distribution<int> uni_dis_dport(rev_dport_min,rev_dport_max);
        dp = uni_dis_dport(gen_dport);
        *udp_dport = htons(dp); // set the destination port 
        chksum += *udp_dport; // and add it to the UDP checksum
      }
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

    if ( unlikely(sent_frames == send_next_latency_frame) )
    {
      // the sent frame was a Latency Frame
      send_ts[latency_timestamp_no++] = rte_rdtsc(); // store its sending timestamp
      send_next_latency_frame = start_latency_frame + latency_timestamp_no * frames_to_send_during_latency_test / num_of_tagged; //prepare the index of the next latency frame
    }
    else
    {
      // the sent frame was a normal Test Frame
      i = (i + 1) % N;
    }

    current_lwB4 = (current_lwB4 + 1) % num_of_lwB4s; // proceed to the next CE element in the CE array
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

int receiveLatency(void *par)
{
  // collecting input parameters:
  class receiverParametersLatency *p = (class receiverParametersLatency *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;
  uint16_t num_of_tagged = p->num_of_tagged;
  uint64_t *receive_ts = p->receive_ts;

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST];                      // pointers for the mbufs of received frames
  uint16_t ipv4 = htons(0x0800);                                  // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6 = htons(0x86DD);                                  // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identificion of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  uint8_t identify_latency[8] = {'I', 'd', 'e', 'n', 't', 'i', 'f', 'y'}; // Identificion of the Latency Frames
  uint64_t *id_lat = (uint64_t *)identify_latency;
  uint64_t received = 0; // number of received frames

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
        if (likely(pkt[20] == 4 && *(uint64_t *)&pkt[82] == *id))
          received++;
        else if (pkt[20] == 4 && *(uint64_t *)&pkt[82] == *id_lat)
        {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[90];
          if (latency_frame_id < 0 || latency_frame_id >= num_of_tagged){
            std::cout <<"Error: Latency IPv4 in IPv6 Frame with invalid frame ID was received!\n"; // to avoid segmentation fault
            return -1;
          }
          receive_ts[latency_frame_id] = timestamp;
          received++; // Latency Frame is also counted as Test Frame
          }
        else if(likely(pkt[20] == 17 && *(uint64_t *)&pkt[62] == *id))
          received++; //bg normal Test Frame
        else if (pkt[20] == 17 && *(uint64_t *)&pkt[62] == *id_lat) //UDP and 'Identify'
        {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[70];
          if (latency_frame_id < 0 || latency_frame_id >= num_of_tagged)
            //rte_exit(EXIT_FAILURE, "Error: Latency Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
            std::cerr << "Error: Latency Background IPv6 Frame with invalid frame ID was received!" << std::endl;  
          receive_ts[latency_frame_id] = timestamp;
          received++; // Latency Frame is also counted as Test Frame
          }
      }
      else if (*(uint16_t *)&pkt[12] == ipv4)
      { /* IPv4 */
         /* check if IPv4 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if (likely(pkt[23] == 17 && *(uint64_t *)&pkt[42] == *id))
          received++;
        else if (pkt[23] == 17 && *(uint64_t *)&pkt[42] == *id_lat)
        {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[50];
          if (latency_frame_id < 0 || latency_frame_id >= num_of_tagged){
            //rte_exit(EXIT_FAILURE, "Error: Latency IPv4 Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
            std::cerr << "Error: Latency IPv4 Frame with invalid frame ID was received!" << std::endl;  
            return -1;
          }
          receive_ts[latency_frame_id] = timestamp;
          received++; // Latency Frame is also counted as Test Frame
        }
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  printf("%s frames received: %lu\n", direction, received);
  return received;
}

void Latency::measure(uint16_t leftport, uint16_t rightport)
{
  senderCommonParametersLatency scp;
  senderParametersLatency spars, spars2;
  receiverParametersLatency rpars, rpars2;

  uint64_t *left_send_ts, *right_send_ts, *left_receive_ts, *right_receive_ts; // pointers for timestamp arrays
  
  scp = senderCommonParametersLatency(ipv6_frame_size,ipv4_frame_size,frame_rate,test_duration,n,m,hz,start_tsc,
                                      number_of_lwB4s,lwB4_array,&ipv6_tunnel,&ipv4_server,
                                      &ipv6_left_bg,&ipv6_right_bg,
                                      fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
                                      rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
                                      first_tagged_delay, num_of_tagged);
  if (forward)
  { // Left to right direction is active
    
    // create dynamic arrays for timestamps
    left_send_ts = new uint64_t[num_of_tagged];
    right_receive_ts = new uint64_t[num_of_tagged];
    if (!left_send_ts || !right_receive_ts){
      std::cerr << "Error: Tester can't allocate memory for timestamps!" << std::endl;
      return;
    }
    
    // fill with 0 (will be used to check, if frame with timestamp was received)
    memset(right_receive_ts, 0, num_of_tagged * sizeof(uint64_t));

    // set individual parameters for the left sender
    // Initialize the parameter class instance
    spars = senderParametersLatency(&scp, pkt_pool_left_sender, leftport, "forward", (ether_addr *)mac_left_dut, (ether_addr *)mac_left_tester,
                          left_send_ts);

    // start left sender
    if (rte_eal_remote_launch(send6Latency, &spars, cpu_left_sender))
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    rpars = receiverParametersLatency(finish_receiving, rightport, "forward", num_of_tagged, right_receive_ts);

    // start right receiver
    if (rte_eal_remote_launch(receiveLatency, &rpars, cpu_right_receiver))
      std::cout << "Error: could not start Right Receiver." << std::endl;
  }

  if (reverse) 
  {
    //std::cout << "REVERSE FORGALOM VAN" << std::endl;
    
    // create dynamic arrays for timestamps
    right_send_ts = new uint64_t[num_of_tagged];
    left_receive_ts = new uint64_t[num_of_tagged];
    if (!right_send_ts || !left_receive_ts){
      std::cerr << "Error: Tester can't allocate memory for timestamps!" << std::endl;
      return;
    }

    // fill with 0 (will be used to chek, if frame with timestamp was received)
    memset(left_receive_ts, 0, num_of_tagged * sizeof(uint64_t));
    
    // set individual parameters for the right sender
    // Initialize the parameter class instance
    spars2 = senderParametersLatency(&scp, pkt_pool_right_sender, rightport, "reverse", (ether_addr *)mac_right_dut, (ether_addr *)mac_right_tester,
                          right_send_ts);

    // start right sender
    if (rte_eal_remote_launch(send4Latency, &spars2, cpu_right_sender))
      std::cout << "Error: could not start Right Sender." << std::endl;
    
    // set parameters for the left receiver
    rpars2 = receiverParametersLatency(finish_receiving, leftport, "reverse", num_of_tagged, left_receive_ts);

    // start left receiver
    if (rte_eal_remote_launch(receiveLatency, &rpars2, cpu_left_receiver))
      std::cout << "Error: could not start Left Receiver." << std::endl; 
  }
  
  std::cout << "Info: Testing started." << std::endl;

  // wait until active senders and receivers finish
  if (forward)
  {
    rte_eal_wait_lcore(cpu_left_sender);
    rte_eal_wait_lcore(cpu_right_receiver);
  }
  if (reverse)
  {
    rte_eal_wait_lcore(cpu_right_sender);
    rte_eal_wait_lcore(cpu_left_receiver);
  }

  // Process the timestamps
  int penalty = 1000 * (test_duration - first_tagged_delay) + stream_timeout; // latency to be reported for lost timestamps, expressed in milliseconds
  if (forward)
    evaluateLatency(num_of_tagged, left_send_ts, right_receive_ts, hz, penalty, "forward");
  if (reverse)
    evaluateLatency(num_of_tagged, right_send_ts, left_receive_ts, hz, penalty, "reverse");


  rte_free(lwB4_array); // release the CEs data memory

  std::cout << "Info: Test finished." << std::endl;
}



void evaluateLatency(uint16_t num_of_tagged, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, int penalty, const char *direction)
{
  double median_latency, worst_case_latency, *latency = new double[num_of_tagged];
  if (!latency){
    rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency values!\n");
    //std::cerr << "Error: Tester can't allocate memory for latency values!" << std::endl;
    //return -1;
  }
  std::cout << "TAGGED: " << num_of_tagged << std::endl;
  for (int i = 0; i < num_of_tagged; i++)
    if (receive_ts[i])
      latency[i] = 1000.0 * (receive_ts[i] - send_ts[i]) / hz; // calculate and exchange into milliseconds
    else
      latency[i] = penalty; // penalty of the lost timestamp
  if (num_of_tagged < 2)
    median_latency = worst_case_latency = latency[0];
  else
  {
    std::sort(latency, latency + num_of_tagged);
    if (num_of_tagged % 2)
      median_latency = latency[num_of_tagged / 2]; // num_of_tagged is odd: median is the middle element
    else
      median_latency = (latency[num_of_tagged / 2 - 1] + latency[num_of_tagged / 2]) / 2; // num_of_tagged is even: median is the average of the two middle elements
    worst_case_latency = latency[int(ceil(0.999 * num_of_tagged)) - 1];                   // WCL is the 99.9th percentile
  }
  printf("%s TL: %lf\n", direction, median_latency);      // Typical Latency
  printf("%s WCL: %lf\n", direction, worst_case_latency); // Worst Case Latency
  
}
struct rte_mbuf *mkLatencyTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    const uint32_t *src_ip, uint32_t *dst_ip, uint16_t id)
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
    rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr));                                      // IPv4 header
    rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));                     // UDP header
    uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)); // UDP data

    mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800); // contains an IPv4 packet
    int ip_length = length - sizeof(rte_ether_hdr);
    mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip); // Does not set IPv4 header checksum
    int udp_length = ip_length - sizeof(rte_ipv4_hdr);   // No IP Options are used
    mkUdpHeader(udp_hd, udp_length); // sets UPD port numbers and checksum to 0.
    int data_length = udp_length - sizeof(rte_udp_hdr);
    mkLatencyData(udp_data, data_length, id);
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

struct rte_mbuf *mkLatencyTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
  const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
  struct in6_addr *src_ip, struct in6_addr *dst_ip, uint16_t id)
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
  mkUdpHeader(udp_hd, udp_length); // sets UPD port numbers and checksum to 0.
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkLatencyData(udp_data, data_length, id);
  udp_hd->dgram_cksum = ~rte_ipv6_udptcp_cksum(ip_hdr, udp_hd); // the uncomplemented UDP checksum is calculated and set
  return pkt_mbuf;
}

struct rte_mbuf *mkLatencyTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6,
    const uint32_t *src_ipv4, uint32_t *dst_ipv4, uint16_t id)
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
  mkUdpHeader(udp_hd, udp_length); // sets UPD port numbers and checksum to 0.
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkLatencyData(udp_data, data_length, id);
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

void mkLatencyData(uint8_t *data, uint16_t length, uint16_t latency_frame_id)
{
  unsigned i;
  uint8_t identify[8] = {'I', 'd', 'e', 'n', 't', 'i', 'f', 'y'}; // Identificion of the Latency Frames
  uint64_t *id = (uint64_t *)identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  *(uint16_t *)data = latency_frame_id;
  data += 2;
  length -= 2;
  
  for (i = 0; i < length; i++){
    data[i] = i % 256;
  }
}

senderCommonParametersLatency::senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                                                            uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
                                                            struct in6_addr *ipv6_tunnel_, uint32_t *ipv4_server_, in6_addr *ipv6_left_bg_, struct in6_addr *ipv6_right_bg_,
                        uint16_t fwd_sport_min_, uint16_t fwd_sport_max_, uint16_t fwd_dport_min_, uint16_t fwd_dport_max_,
                        uint16_t rev_sport_min_, uint16_t rev_sport_max_, uint16_t rev_dport_min_, uint16_t rev_dport_max_,
                                                            uint16_t first_tagged_delay_, uint16_t num_of_tagged_) : senderCommonParameters(ipv6_frame_size_, ipv4_frame_size_, frame_rate_,  test_duration_,
                                                                                                             n_,  m_,  hz_,  start_tsc_,  number_of_lwB4s_,  lwB4_array_, ipv6_tunnel_, 
                                                                                                             ipv4_server_, ipv6_left_bg_, ipv6_right_bg_, 
                        fwd_sport_min_, fwd_sport_max_, fwd_dport_min_, fwd_dport_max_,
                        rev_sport_min_, rev_sport_max_, rev_dport_min_, rev_dport_max_
  )
  {
    first_tagged_delay = first_tagged_delay_;
    num_of_tagged = num_of_tagged_;
  }

  senderCommonParametersLatency::senderCommonParametersLatency(){}  

    
senderParametersLatency::senderParametersLatency(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                                                struct ether_addr *dst_mac_, struct ether_addr *src_mac_,
                                                uint64_t *send_ts_) : senderParameters(cp_, pkt_pool_, eth_id_, direction_, dst_mac_, src_mac_
    )
    {
        send_ts = send_ts_;
    }

senderParametersLatency::senderParametersLatency(){}

receiverParametersLatency::receiverParametersLatency(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_, uint16_t num_of_tagged_, uint64_t *receive_ts_) : receiverParameters(finish_receiving_, eth_id_, direction_)
{
    num_of_tagged = num_of_tagged_;
    receive_ts = receive_ts_;
}

receiverParametersLatency::receiverParametersLatency(){}
