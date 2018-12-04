#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <caputils/caputils.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <getopt.h>

#include "extract.hpp"

static int show_zero = 0;
static const char* iface = NULL;
const char* program_name = NULL;

static void handle_sigint(int signum){
	if ( !keep_running ){
		fprintf(stderr, "\rGot SIGINT again, terminating.\n");
		abort();
	}
	fprintf(stderr, "\rAborting capture.\n");
	keep_running = false;
}

class Output {
public:
	virtual void write_header(double sampleFrequency, double tSample){};
	virtual void write_trailer(){};
	virtual void write_sample(double t, unsigned long pkts) = 0;
};

class DefaultOutput: public Output {
public:
	virtual void write_header(double sampleFrequency, double tSample){
		fprintf(stdout, "sampleFrequency: %.2fHz\n", sampleFrequency);
		fprintf(stdout, "tSample:         %fs\n", tSample);
		fprintf(stdout, "\n");
		fprintf(stdout, "Time                      \t   Packets\n");
	}

	virtual void write_sample(double t, unsigned long pkts){
		fprintf(stdout, "%.15f\t%10ld\n", t, pkts);
	}
};

class CSVOutput: public Output {
public:
	CSVOutput(char delimiter, bool show_header)
		: delimiter(delimiter)
		, show_header(show_header){

	}

	virtual void write_header(double sampleFrequency, double tSample){
		if ( show_header ){
			fprintf(stdout, "\"Time (tSample: %f)\"%c\"Packets\"\n", tSample, delimiter);
		}
	}

	virtual void write_sample(double t, unsigned long pkts){
		fprintf(stdout, "%.15f%c%ld\n", t, delimiter, pkts);
	}

private:
	char delimiter;
	bool show_header;
};

class PacketRate: public Extractor {
public:
	PacketRate()
		: Extractor()
		, pkts(0){

		set_formatter(FORMAT_DEFAULT);
	}

	virtual void set_formatter(enum Formatter format){
		switch (format){
		case FORMAT_DEFAULT: output = new DefaultOutput; break;
		case FORMAT_CSV:     output = new CSVOutput(';', false); break;
		case FORMAT_TSV:     output = new CSVOutput('\t', false); break;
		case FORMAT_MATLAB:  output = new CSVOutput('\t', true); break;
		}
	}

	using Extractor::set_formatter;

	virtual void reset(){
		pkts = 0;
		Extractor::reset();
	}

protected:
	virtual void write_header(int index){
		output->write_header(sampleFrequency, to_double(tSample));
	}

	virtual void write_trailer(int index){
		output->write_trailer();
	}

	virtual void write_sample(double t){
		if ( show_zero || pkts > 0 ){
			output->write_sample(t, pkts);
		}

		pkts = 0;
	}

	virtual void accumulate(qd_real fraction, unsigned long packet_bits, const cap_head* cp, int counter){
		if ( counter == 1 ){
			pkts += 1;
		}
	}

private:
	Output* output;
	unsigned long pkts;
};

static const char* short_options = "p:i:q:m:f:zxtTh";
static struct option long_options[]= {
	{"packets",          required_argument, 0, 'p'},
	{"iface",            required_argument, 0, 'i'},
	{"level",            required_argument, 0, 'q'},
	{"sampleFrequency",  required_argument, 0, 'm'},
	{"format",           required_argument, 0, 'f'},
	{"show-zero",        no_argument,       0, 'z'},
	{"no-show-zero",     no_argument,       0, 'x'},
	{"relative-time",    no_argument,       0, 't'},
	{"absolute-time",    no_argument,       0, 'T'},
	{"help",             no_argument,       0, 'h'},
	{0, 0, 0, 0} /* sentinel */
};

static void show_usage(void){
	printf("%s-" VERSION " (libcap_utils-%s)\n", program_name, caputils_version(NULL));
	printf("(C) 2012 David Sveningsson <david.sveningsson@bth.se>\n");
	printf("Usage: %s [OPTIONS] STREAM\n", program_name);
	printf("  -i, --iface                 For ethernet-based streams, this is the interface to listen\n"
	       "                              on. For other streams it is ignored.\n"
	       "  -m, --sampleFrequency       Sampling frequency in Hz. Valid prefixes are 'k', 'm' and 'g'.\n"
	       "  -q, --level 		            Level to calculate bitrate {physical (default), link, network, transport and application}\n"
	       "                              At level N , payload of particular layer is only considered, use filters to select particular streams.\n"
	       "                              To calculate the bitrate at physical , use physical layer, Consider for Network layer use [-q network]\n"
	       "                              It shall contain transport protocol header + payload\n"
	       "                                - link: all bits captured at physical level, i.e link + network + transport + application\n"
	       "                                - network: payload field at link layer , network + transport + application\n"
	       "                                - transport: payload at network  layer, transport + application\n"
	       "                                - application: The payload field at transport leve , ie.application\n"
	       "                              Default is link\n"
	       "  -p, --packets=N             Stop after N packets.\n"
	       "  -z, --show-zero             Show bitrate when zero.\n"
	       "  -x, --no-show-zero          Don't show bitrate when zero [default]\n"
	       "  -f, --format=FORMAT         Set a specific output format. See below for list of supported formats.\n"
	       "  -t, --relative-time         Show timestamps relative to the first packet.\n"
	       "  -T, --absolute-time         Show timestamps with absolute values (default).\n"
	       "  -h, --help                  This text.\n\n");


	output_format_list();
	filter_from_argv_usage();
}

int main(int argc, char **argv){
	/* extract program name from path. e.g. /path/to/MArCd -> MArCd */
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

	PacketRate app;

	int op, option_index = -1;
	while ( (op = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1 ){
		switch (op){
		case 0:   /* long opt */
		case '?': /* unknown opt */
			break;

		case 'f': /* --format */
			app.set_formatter(optarg);
			break;

		case 'p':
			app.set_max_packets(atoi(optarg));
			break;

		case 'm' : /* --sampleFrequency */
			app.set_sampling_frequency(optarg);
			break;

		case 'q': /* --level */
			app.set_extraction_level(optarg);
			break;

		case 'l': /* --link */
			app.set_link_capacity(optarg);
			break;

		case 'i':
			iface = optarg;
			break;

		case 'z':
			show_zero = 1;
			break;

		case 'x':
			show_zero = 0;
			break;

		case 't': /* --relative-time */
			app.set_relative_time(true);
			break;

		case 'T': /* --absolute-time */
			app.set_relative_time(false);
			break;

		case 'h':
			show_usage();
			return 0;

		default:
			fprintf (stderr, "%s: ?? getopt returned character code 0%o ??\n", program_name, op);
		}
	}

	/* handle C-c */
	signal(SIGINT, handle_sigint);

	int ret;

	/* Open stream(s) */
	stream_t stream;
	if ( (ret=stream_from_getopt(&stream, argv, optind, argc, iface, "-", program_name, 0)) != 0 ) {
		return ret; /* Error already shown */
	}
	stream_print_info(stream, stderr);

	app.reset();
	app.process_stream(stream, &filter);

	/* Release resources */
	stream_close(stream);
	filter_close(&filter);

	return 0;
}
