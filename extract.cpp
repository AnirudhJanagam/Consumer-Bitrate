#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "extract.hpp"
#include <caputils/packet.h>

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

/* the caputils/marker.h header wasn't c++ compatible so I include the declaration here instead */
extern "C" int is_marker(const struct cap_header* cp, struct marker* ptr, int port);

bool keep_running = true;
extern const char* program_name;

void output_format_list(){
	printf("Supported output formats:\n");
	const struct formatter_entry* cur = formatter_lut;
	while ( cur->name ){
		printf(" * %-10s (%s)\n", cur->name, cur->desc);
		cur++;
	}
	printf("\n");
}

static int prefix_to_multiplier(char prefix){
	prefix = tolower(prefix);
	switch ( prefix ){
	case 0: return 1;
	case 'k': return 1e3;
	case 'm': return 1e6;
	case 'g': return 1e9;
	default: return -1;
	}
}

/**
 * Get prefix from number represented by a string and removes the prefix by
 * setting it to NULL.
 * If no prefix was found it returns 0.
 * E.g. "100k" -> 'k'.
 */
static char pop_prefix(char* string){
	if ( *string == 0 ) return 0;

	const size_t offset = strlen(string) - 1;
	if ( ! isalpha(string[offset]) ){
		return 0;
	}

	const char prefix = string[offset];
	string[offset] = 0;
	return prefix;
}

Extractor::Extractor()
	: ignore_marker(false)
	, first_packet(true)
	, relative_time(false)
	, max_packets(0)
	, level(LEVEL_LINK) {

	set_sampling_frequency(1.0); /* default to 1Hz */
	set_link_capacity("100m");   /* default to 100mbps */
}

Extractor::~Extractor(){

}

void Extractor::set_ignore_marker(bool state){
	ignore_marker = state;
}

void Extractor::set_sampling_frequency(double hz){
	sampleFrequency = hz;
	tSample = 1.0 / sampleFrequency;
}

void Extractor::set_sampling_frequency(const char* str){
	char* tmp = strdup(str);
	const char prefix = pop_prefix(tmp);
	int multiplier = prefix_to_multiplier(prefix);

	if ( multiplier == -1 ){
		fprintf(stderr, "unknown prefix '%c' for --sampleFrequency, ignored.\n", prefix);
		multiplier = 1;
	}

	set_sampling_frequency(atof(tmp) * multiplier);
	free(tmp);
}

void Extractor::set_max_packets(size_t n){
	max_packets = n;
}

void Extractor::set_link_capacity(unsigned long bps){
	link_capacity = bps;
}

void Extractor::set_link_capacity(const char* str){
	char* tmp = strdup(str);
	const char prefix = pop_prefix(tmp);
	int multiplier = prefix_to_multiplier(prefix);

	if ( multiplier == -1 ){
		fprintf(stderr, "unknown prefix '%c' for --linkCapacity, ignored.\n", prefix);
		multiplier = 1;
	}

	set_link_capacity(atof(tmp) * multiplier);
	free(tmp);
}

void Extractor::set_extraction_level(const char* str){
	level = level_from_string(str);
	if ( level == LEVEL_INVALID ){
		fprintf(stderr, "%s: unrecognised level \"%s\", defaulting to \"link\".\n", program_name, str);
		level = LEVEL_LINK;
	}
}

void Extractor::set_relative_time(bool state){
	relative_time = state;
}

void Extractor::set_formatter(const char* str){
	const struct formatter_entry* cur = formatter_lut;
	while ( cur->name ){
		if ( strcasecmp(cur->name, str) == 0 ){
			return set_formatter(cur->fmt);
		}
		cur++;
	}

	fprintf(stderr, "%s: unrecognised formatter \"%s\", ignored.\n", program_name, str);
}

void Extractor::reset(){
	first_packet = true;
	counter = 1;
}

void Extractor::process_stream(const stream_t st, struct filter* filter){
	static int index = 0;
	const stream_stat_t* stat = stream_get_stat(st);
	int ret = 0;

	write_header(index);

	while ( keep_running && ( max_packets == 0 || stat->matched < max_packets ) ) {
		/* A short timeout is used to allow the application to "breathe", i.e
		 * terminate if SIGINT was received. */
		struct timeval tv = {1,0};

		/* Read the next packet */
		cap_head* cp;
		ret = stream_read(st, &cp, filter, &tv);
		if ( ret == EAGAIN ){
			if ( !first_packet ){
				do_sample();
			}
			continue; /* timeout */
		} else if ( ret != 0 ){
			break; /* shutdown or error */
		}

		calculate_samples(cp);
	}

	/* push the final sample */
	do_sample();

	/* only write trailer if app isn't terminating */
	if ( keep_running ){
		write_trailer(index++);
	}

	/* if ret == -1 the stream was closed properly (e.g EOF or TCP shutdown)
	 * In addition EINTR should not give any errors because it is implied when the
	 * user presses C-c */
	if ( ret > 0 && ret != EINTR ){
		fprintf(stderr, "stream_read() returned 0x%08X: %s\n", ret, caputils_error_string(ret));
	}
}

qd_real Extractor::estimate_transfertime(unsigned long bits){
	return qd_real((double)bits) / link_capacity;
}

bool Extractor::valid_first_packet(const cap_head* cp){
	if ( !ignore_marker ) return true;

	/* ignore marker packets */
	if ( is_marker(cp, nullptr, 0) ){
		return false;
	}

	/* ignore initial ICMP packets which is a response to the marker packet being
	   undeliverable */
	const struct ethhdr* ethhdr = cp->ethhdr;
	const struct ip* ip = find_ipv4_header(ethhdr, nullptr);
	if ( ip && ip->ip_p == IPPROTO_ICMP ){
		const struct icmphdr* icmp = (const struct icmphdr*)((char*)ip + 4*ip->ip_hl);
		if ( icmp->type == ICMP_DEST_UNREACH && icmp->code == ICMP_PORT_UNREACH ){
			return false;
		}
	}

	return true;
}

void Extractor::calculate_samples(const cap_head* cp){
	const unsigned long packet_bits = layer_size(level, cp) * 8;
	const qd_real current_time = qd_real((double)cp->ts.tv_sec) + qd_real((double)cp->ts.tv_psec/PICODIVIDER);
	const qd_real transfertime_packet = estimate_transfertime(packet_bits);

	if ( first_packet ) {
		if ( !valid_first_packet(cp) ){
			return;
		}

		ref_time = current_time;
		start_time = ref_time;
		end_time = ref_time + tSample;
		first_packet = false;
	}

	while ( keep_running && current_time >= end_time ){
		do_sample();
	}

	/* split large packets into multiple samples */
	int packet_samples = 1;
	qd_real remaining_transfertime = transfertime_packet;
	remaining_samplinginterval = end_time - current_time;
	while ( keep_running && remaining_transfertime >= remaining_samplinginterval ){
		const qd_real fraction = remaining_samplinginterval / transfertime_packet;
		accumulate(fraction, packet_bits, cp, packet_samples++);
		remaining_transfertime -= remaining_samplinginterval;
		do_sample();
	}

	/* If the previous loop was broken by keep_running we should not sample the remaining data */
	if ( !keep_running ) return;

	// handle small packets or the remaining fractional packets which are in next interval
	const qd_real fraction = remaining_transfertime / transfertime_packet;
	accumulate(fraction, packet_bits, cp, packet_samples++);
	remaining_samplinginterval = end_time - current_time - transfertime_packet;
}

void Extractor::do_sample(){
	const double t = to_double(relative_time ? (start_time - ref_time) : start_time);
	write_sample(t);

	// reset start_time ; end_time; remaining_sampling interval
	start_time = ref_time + counter++ * tSample;
	end_time = start_time + tSample;
	remaining_samplinginterval = tSample;
}

void Extractor::write_header(int index){
	/* do nothing */
}

void Extractor::write_trailer(int index){
	/* do nothing */
}
