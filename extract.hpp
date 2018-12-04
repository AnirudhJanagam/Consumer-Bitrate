#ifndef EXTRACT_H
#define EXTRACT_H

#include <caputils/caputils.h>
#include <caputils/packet.h>
#include <qd/qd_real.h>

enum Formatter {
	FORMAT_DEFAULT = 500,             /* Human-readable */
	FORMAT_CSV,                       /* CSV (semi-colon separated) */
	FORMAT_TSV,                       /* TSV (tab-separated) */
	FORMAT_MATLAB,                    /* Matlab format (TSV with header) */
};

struct formatter_entry { const char* name; const char* desc; enum Formatter fmt; };
const struct formatter_entry formatter_lut[] = {
	{"default", "default format",       FORMAT_DEFAULT},
	{"csv",     "semi-colon separated", FORMAT_CSV},
	{"tsv",     "tab-separated",        FORMAT_TSV},
	{"matlab",  "suitable for matlab",  FORMAT_MATLAB},
	{nullptr, nullptr, (enum Formatter)0} /* sentinel */
};

void output_format_list();

/**
 * Controls whenever the application should run or not.
 */
extern bool keep_running;

/**
 * This class reads packets, splits them into time-based interval, and calls
 * Extractor::accumulate. To calculate bitrate simply add the number of bits
 * until a sample is ready.
 */
class Extractor {
public:
	Extractor();
	virtual ~Extractor();

	/**
	 * Force counters and accumulators to reset.
	 */
	virtual void reset();

	/**
	 * Process packets in stream.
	 */
	void process_stream(const stream_t st, struct filter* filter);

	/**
	 * Stop processing packets.
	 * This has the same effect as setting the global keep_running to false.
	 */
	void stop();

	/**
	 * Ignore the initial marker packet.
	 */
	void set_ignore_marker(bool state);

	/**
	 * Set sampling frequency in Hz.
	 */
	void set_sampling_frequency(double hz);

	/**
	 * Set sampling frequency from string.
	 * Supports prefixes: 'k', 'm', and 'g'.
	 */
	void set_sampling_frequency(const char* str);

	/**
	 * Set level to extract size from.
	 */
	void set_extraction_level(const char* str);

	/**
	 * Stop processing after N packets.
	 */
	void set_max_packets(size_t n);

	/**
	 * Set link capacity in bits per second.
	 */
	void set_link_capacity(unsigned long bps);

	/**
	 * Set link capacity from string.
	 * Supports prefixes: 'k', 'm', 'g'.
	 */
	void set_link_capacity(const char* str);

	/**
	 * Use timestamps relative to the first packet.
	 * Default is false.
	 */
	void set_relative_time(bool state);

	/**
	 * Set the output formatter.
	 * If the app does not handle a specific format it should warn and set to default.
	 */
	virtual void set_formatter(enum Formatter format) = 0;
	void set_formatter(const char* str);

protected:
	/**
	 * Write header. Called before the first packet is processed.
	 * @param index An incrementing counter (beginning at 0).
	 */
	virtual void write_header(int index);

	/**
	 * Write trailer. Called after the last packet is processed.
	 * @param index An incrementing counter (beginning at 0).
	 */
	virtual void write_trailer(int index);

	/**
	 * Write a sample.
	 */
	virtual void write_sample(double t) = 0;

	/**
	 * Accumulate value from packet.
	 *
	 * @param fraction The fraction (0-1) of the packet this sample should contain.
	 * @param bits Total number of bits in packet, extracted at requested level.
	 * @param cp Packet header.
	 * @param counter Number of times this packet has been sampled.
	 */
	virtual void accumulate(qd_real fraction, unsigned long bits, const cap_head* cp, int counter) = 0;

	/**
	 * Calculate bitrate for current sample and move time forward.
	 */
	void do_sample();

	/**
	 * Estimate how long it takes (in seconds) to N bits over the current link speed.
	 */
	qd_real estimate_transfertime(unsigned long bits);

	qd_real ref_time;
	qd_real start_time;
	qd_real end_time;
	qd_real remaining_samplinginterval;
	double sampleFrequency;
	qd_real tSample;
	int counter;

private:
	void calculate_samples(const cap_head* cp);
	bool valid_first_packet(const cap_head* cp);

	bool ignore_marker;
	bool first_packet;
	bool relative_time;
	unsigned int max_packets;
	unsigned long link_capacity;
	enum Level level;
};

#endif /* EXTRACT_H */
