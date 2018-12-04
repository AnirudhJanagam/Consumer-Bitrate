//we are at ip protocol capinfo

//#define __STDC_FORMAT_MACROS

//#ifdef HAVE_CONFIG_H
//#include "config.h"
//#endif /* HAVE_CONFIG_H */

#include "caputils/caputils.h"
#include "caputils/stream.h"
#include "caputils/filter.h"
#include "caputils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <sys/time.h>
#include <netdb.h>
#include <cstdlib>
#include <map>
#include <vector>
#include <string.h>
#include <cstring>
#include <string>
#include <iostream>
#include <qd/qd_real.h>
#include <math.h>
#include <iomanip>
//#define debug
#define STPBRIDGES 0x0026
#define CDPVTP 0x016E

using namespace std;

static int keep_running = 1;
static int print_content = 0;
static int print_date = 0;
static int max_packets = 0;
static const char* iface = NULL;
static struct timeval timeout = {1,0};
static const char* program_name = NULL;


qd_real remaining_samplinginterval;
qd_real ref_time;
qd_real start_time; // has to initialise when the first packet is read
qd_real end_time; //has to initialise till the next interval
qd_real tSample;
qd_real bitrate; // make it qd_real 
qd_real crap1[100];
qd_real bits; // make bits as qd
qd_real crap2[100];


long int packets_count;
uint64_t n = 0;

void handle_sigint(int signum){
	if ( keep_running == 0 ){
		fprintf(stderr, "\rGot SIGINT again, terminating.\n");
		abort();
	}
	fprintf(stderr, "\rAborting capture.\n");
	keep_running = 0;
}

/*
  Created:      2003-02-20 12:40, Patrik.Carlsson@bth.se
  Latest edit:  2003-02-20 10:30, Patrik.Carlsson@bth.se

  Function:
  int payLoadExtraction(*)
  Return value:
  Payload at given level.

  Arguments (In Order):
  Desired level, 0 physical, 1 link, 2 network, 3 transport
  Description:
  Returns the number of bytes a packet contains at level L



*/
//caphead is new one, data is old.

void printbitrate() {
	//calculate bitrate
	bitrate = bits /(tSample);
	//print bitrate greater than zero
	if (to_double(bits) > 0){
	  cout  << start_time.to_string(25)<<"\t"<<bitrate.to_string(25) <<"\t " << "bits " << bits.to_string(15) << "\ttsample: " << tSample.to_string(15) << "bits (double) " << to_double(bits) << "toString bits == " << bits.to_string() << "ENOD" << endl;
		//setiosflags(ios::fixed) << setprecision(15)
	}
	// reset start_time ; end_time; remaining_sampling interval
	start_time = end_time;
	end_time = ++n*tSample;
	remaining_samplinginterval = tSample;
	bits = 0.0;

}
static void show_usage(void){
//	printf("%s-" VERSION "\n", program_name);
	printf("(C) 2004 Patrik Arlos <patrik.arlos@bth.se>\n");
	printf("(C) 2012 David Sveningsson <david.sveningsson@bth.se>\n");
	printf("(C) 2012 Vamsi krishna Konakalla <vkk@bth.se>\n");
	printf("Usage: %s [OPTIONS] STREAM\n", program_name);
	printf("  -c, --content        Write full package content as hexdump. [default=no]\n"
	       "  -i, --iface          For ethernet-based streams, this is the interface to listen\n"
	       "                       on. For other streams it is ignored.\n"
	       "  -m, --samplingFrequency Sampling frequency in Hertz \n"
		"  -q, --level 		        Level to calculate bitrate {physical (default), link, network, transport and application}\n"
	        "                         At level N , payload of particular layer is only considered, use filters to select particular streams.\n"
	        "                         To calculate the bitrate at physical , use physical layer, Consider for Network layer use [-q network]\n"
	        "                         It shall contain transport protocol header + payload\n"
	        "                           - link: all bits captured at physical level, i.e link + network + transport + application\n"
	        "                           - network: payload field at link layer , network + transport + application\n"
	        "                           - transport: payload at network  layer, transport + application\n"
	        "                           - application: The payload field at transport leve , ie.application\n"
	        "                         Default is link\n"
	       "  -l, --linkCapacity   link Capacity in bits per second default 100 Mbps, (eg.input 100e6) \n"
	       "  -p, --packets=N      Stop after N packets.\n"
	       "  -t, --timeout=N      Wait for N ms while buffer fills [default: 1000ms].\n"
	       "  -d, --calender       Show timestamps in human-readable format.\n"
	       "  -h, --help           This text.\n\n");
	filter_from_argv_usage();
}

static int payLoadExtraction(int level, const cap_head* caphead) {
	// payload size at physical (ether+network+transport+app)
	if ( level==0 ) {
		return caphead->len;
	};

	// payload size at link  (network+transport+app)
	if ( level==1 ) {
		return caphead->len - sizeof(struct ethhdr);
	};

	const struct ethhdr *ether = caphead->ethhdr;
	const struct ip* ip_hdr = NULL;
	struct tcphdr* tcp = NULL;
	struct udphdr* udp = NULL;
	size_t vlan_offset = 0;

	switch(ntohs(ether->h_proto)) {
	case ETHERTYPE_IP:/* Packet contains an IP, PASS TWO! */
		ip_hdr = (struct ip*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr));
	  ipv4:

		// payload size at network  (transport+app)
		if ( level==2 ) {
			return ntohs(ip_hdr->ip_len)-4*ip_hdr->ip_hl;
		};

		switch(ip_hdr->ip_p) { /* Test what transport protocol is present */
		case IPPROTO_TCP: /* TCP */
			tcp = (struct tcphdr*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr) + vlan_offset + 4*ip_hdr->ip_hl);
			if(level==3) return ntohs(ip_hdr->ip_len)-4*tcp->doff-4*ip_hdr->ip_hl;  // payload size at transport  (app)
			break;
		case IPPROTO_UDP: /* UDP */
			udp = (struct udphdr*)(caphead->payload + sizeof(cap_header) + sizeof(struct ethhdr) + vlan_offset + 4*ip_hdr->ip_hl);
			if(level==3) return ntohs(udp->len)-8;                     // payload size at transport  (app)
			break;
		default:
			fprintf(stderr, "Unknown IP transport protocol: %d\n", ip_hdr->ip_p);
			return 0; /* there is no way to know the actual payload size here */
		}
		break;

	case ETHERTYPE_VLAN:
		ip_hdr = (struct ip*)(caphead->payload + sizeof(cap_header) + sizeof(struct ether_vlan_header));
		vlan_offset = 4;
		goto ipv4;

	case ETHERTYPE_IPV6:
		fprintf(stderr, "IPv6 not handled, ignored\n");
		return 0;

	case ETHERTYPE_ARP:
		fprintf(stderr, "ARP not handled, ignored\n");
		return 0;

	case STPBRIDGES:
		fprintf(stderr, "STP not handled, ignored\n");
		return 0;

	case CDPVTP:
		fprintf(stderr, "CDPVTP not handled, ignored\n");
		return 0;

	default:      /* Packet contains unknown link . */
		fprintf(stderr, "Unknown ETHERTYPE 0x%0x \n", ntohs(ether->h_proto));
		return 0; /* there is no way to know the actual payload size here, a zero will ignore it in the calculation */
	}

	fprintf(stderr, "packet wasn't handled by payLoadExtraction, ignored\n");
	return 0;
}

int main(int argc, char **argv){
  /* extract program name from path. e.g. /path/to/MArCd -> MArCd */
  double sampleFrequency = 1.0; //default 1hz
  tSample = 1.0/sampleFrequency;
  double linkCapacity = 100e6;
  int payLoadSize;
  int level = 0;
  bits = 0.0;
  bitrate = 0.0;
  const char* separator = strrchr(argv[0], '/');
  if ( separator ){
    program_name = separator + 1;
  } else {
    program_name = argv[0];
  }

	struct filter filter;
	if ( filter_from_argv(&argc, argv, &filter) != 0 ){
		return 0; /* error already shown */
	}

	filter_print(&filter, stderr, 0);
   
	int op, option_index = -1;
	static struct option long_options[]= {
	{"content",  no_argument,       0, 'c'},
	{"packets",  required_argument, 0, 'p'},
	{"iface",    required_argument, 0, 'i'},
	{"timeout",  required_argument, 0, 't'},
	{"level",  required_argument, 0, 'q'},
	{"sampleFrequency",  required_argument, 0, 'm'},
	{"linkCapacity",  required_argument, 0, 'l'},
	{"calender", no_argument,       0, 'd'},
	{"help",     no_argument,       0, 'h'},
	{0, 0, 0, 0} /* sentinel */
};
	while ( (op = getopt_long(argc, argv, "hcdi:p:t:m:q:l:", long_options, &option_index)) != -1 ){
		switch (op){
		case 0:   /* long opt */
		case '?': /* unknown opt */
			break;

		case 'd':
			print_date = 1;
			break;

		case 'p':
			max_packets = atoi(optarg);
			break;
		case 'm' : /* --samplefrequency */
			sampleFrequency = atof (optarg);
			tSample = 1/(double) sampleFrequency;
			break;
	

		case 't':
			{
				int tmp = atoi(optarg);
				timeout.tv_sec  = tmp / 1000;
				timeout.tv_usec = tmp % 1000 * 1000;
			}
			break;
		case 'q': /* --level */
			if (strcmp (optarg, "link") == 0)
				level = 0;
			else if ( strcmp (optarg, "network" ) == 0)
				level = 1;
			else if (strcmp (optarg ,"transport") == 0)
				level = 2;
			else if (strcmp (optarg , "application") == 0)
				level = 3;
			else {
				fprintf(stderr, "unrecognised level arg %s \n", optarg);
				exit(1);
			}
			break;

		case 'l': /* --link */
			linkCapacity = atof (optarg);
			cout << " Link Capacity input = " << linkCapacity << " bps\n";
			break;

		case 'c':
			print_content = 1;
			break;

		case 'i':
			iface = optarg;
			break;

		case 'h':
			show_usage();
			return 0;

		default:
			printf ("?? getopt returned character code 0%o ??\n", op);
		}
	}

	int ret;
	// initialise packet count
     packets_count = 0;
	/* Open stream(s) */
	struct stream* stream;
	if ( (ret=stream_from_getopt(&stream, argv, optind, argc, iface, "-", program_name, 0)) != 0 ) {
		return ret; /* Error already shown */
	}
	const stream_stat_t* stat = stream_get_stat(stream);
	stream_print_info(stream, stderr);

	/* handle C-c */
	signal(SIGINT, handle_sigint);

	while ( keep_running ) {
#ifdef debug 
	  cout << "New Packet  we start with " << to_double(bits) << " bits\n";
#endif
		/* A short timeout is used to allow the application to "breathe", i.e
		 * terminate if SIGINT was received. */
		struct timeval tv = timeout;

		/* Read the next packet */
		cap_head* cp;
		ret = stream_read(stream, &cp, &filter, &tv);
		if ( ret == EAGAIN ){
			continue; /* timeout */
		} else if ( ret != 0 ){
			break; /* shutdown or error */
		} 
                qd_real pkt1;
				payLoadSize = payLoadExtraction(level, cp); //payload size
#ifdef debug
				cout<< "Payload is " << payLoadSize <<"\n";
#endif
				cout << "1New Packet  we start with " << to_double(bits) << " bits\n";
                pkt1=(qd_real)(double)cp->ts.tv_sec+(qd_real)(double)(cp->ts.tv_psec/PICODIVIDER); // extract timestamp.
		packets_count ++;
		if (packets_count == 1) {
		  ref_time = pkt1;
		  start_time = ref_time;
		  end_time = ref_time + tSample;
		  remaining_samplinginterval = end_time - start_time;
		}
		cout << "2New Packet  we start with " << to_double(bits) << " bits\n";
		const qd_real diff = pkt1 - end_time;
                // while current timestamp - tend > sampling interval print bitrate. 
		/*
#ifdef debug
		cout << setiosflags(ios::fixed) << setprecision(14) << "PKT timestamp is : " << to_double(pkt1)<<"\t"<<" End of the sample time end_time :"
		<<to_double(end_time) <<"Difference : " <<to_double(diff)  <<"\n"; 
		cout << "fabs: " << fabs(to_double(diff)) << "bits :== =" << to_double(bits) << endl;
#endif		
		*/
		cout << "3New Packet  we start with " << to_double(bits) << " bits\n";
		while ( pkt1 >= end_time || fabs(to_double(pkt1-end_time)) < 0.00000000001 )
		{
			printbitrate();
		}  
		// estimate transfer time of the packet
		qd_real remaining_transfertime, transfertime_packet;
		transfertime_packet = (payLoadSize*8)/linkCapacity;
		remaining_transfertime = transfertime_packet;
		remaining_samplinginterval = end_time - pkt1; //added now
#ifdef debug 
		cout << setiosflags(ios::fixed) << setprecision(12) << "Estimated transfer time of this packet is : " << to_double(transfertime_packet)<<"\n"<<" Estimating sampling interval left is :"
		<<to_double(remaining_samplinginterval) <<"\n"; 
		//in case packet is small the packet will not enter this loop, now handle the big packet
#endif   		// 		
		while ( remaining_transfertime >= remaining_samplinginterval )
		{
#ifdef debug
			cout <<"Hello World \n";
#endif
			bits += ((remaining_samplinginterval)/(transfertime_packet))*payLoadSize*8;
			remaining_transfertime-=remaining_samplinginterval;
#ifdef debug 		
			cout << setiosflags(ios::fixed) << setprecision(12) << to_double(remaining_transfertime)<<":RTT:RSI:"<< to_double(remaining_samplinginterval) <<":BITS"<<to_double(bits) <<"\n";
#endif 
			printbitrate(); // print bitrate -- dont forget to reset the remaining sampling interval in print_bitrate; set it to tSample; reset Bits;
			bits=0.0;
		}
		// handle small packets or the remaining fractional packets which are in next interval
		bits+= (((remaining_transfertime))/((transfertime_packet)))*payLoadSize*8;
		remaining_samplinginterval = end_time - pkt1 - transfertime_packet;
#ifdef debug 
		cout << setiosflags(ios::fixed) << setprecision(12) << to_double(remaining_transfertime)<<":RTT:RSII:"<< to_double(remaining_samplinginterval) <<":BITS after"<<to_double(bits) <<"\n";
		cout << setiosflags(ios::fixed) << setprecision(12) << "Estimated sample interval after transfer is : " << to_double(remaining_samplinginterval)<<"\n";
#endif 
		// handle remaining sampling interval for small packets;
		
		//fprintf(stdout, "[%4"PRIu64"]:%.4s:%.8s:", stat->matched, cp->nic, cp->mampid);
		if( print_date == 0 ) {
			//fprintf(stdout, "%u.", cp->ts.tv_sec);
		} else {
			//static char timeStr[25];
			//struct tm tm = *gmtime(&time);
			//strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm);
		//	fprintf(stdout, "%s.", timeStr);
		}

		//fprintf(stdout, "%012"PRId64":LINK(%4d):CAPLEN(%4d):", cp->ts.tv_psec, cp->len, cp->caplen);
                //int packetlength = cp->len;
		

		if ( max_packets > 0 && stat->matched >= max_packets) {
			/* Read enough pkts lets break. */
			printf("read enought packages\n");
			break;
		}
	}

	/* if ret == -1 the stream was closed properly (e.g EOF or TCP shutdown)
	 * In addition EINTR should not give any errors because it is implied when the
	 * user presses C-c */
	if ( ret > 0 && ret != EINTR ){
		fprintf(stderr, "stream_read() returned 0x%08X: %s\n", ret, caputils_error_string(ret));
	}

	/* Write stats */
	//fprintf(stderr, "%"PRIu64" packets received.\n", stat->read);
	//fprintf(stderr, "%"PRIu64" packets matched filter.\n", stat->matched);

	/* Release resources */
	stream_close(stream);
	filter_close(&filter);

	return 0;
}




