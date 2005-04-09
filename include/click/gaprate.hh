// -*- c-basic-offset: 4; related-file-name: "../../lib/gaprate.cc" -*-
#ifndef CLICK_GAPRATE_HH
#define CLICK_GAPRATE_HH
#include <click/timestamp.hh>
CLICK_DECLS
class ErrorHandler;

class GapRate { public:

    inline GapRate();
    inline GapRate(unsigned);

    unsigned rate() const			{ return _rate; }
  
    inline void set_rate(unsigned);
    void set_rate(unsigned, ErrorHandler *);
    inline void reset();

    inline bool need_update(const Timestamp &);
    void update()				{ _sec_count++; }
    void update_with(int incr)			{ _sec_count += incr; }

    enum { UGAP_SHIFT = 12 };
    enum { MAX_RATE = 1000000U << UGAP_SHIFT };

  private:
  
    unsigned _ugap;
    int _sec_count;
    long _tv_sec;
    unsigned _rate;
#if DEBUG_GAPRATE
    Timestamp _last;
#endif

};

inline void
GapRate::reset()
{
    _tv_sec = -1;
#if DEBUG_GAPRATE
    _last._sec = 0;
#endif
}

inline void
GapRate::set_rate(unsigned rate)
{
    if (rate > MAX_RATE)
	rate = MAX_RATE;
    _rate = rate;
    _ugap = (rate == 0 ? MAX_RATE + 1 : MAX_RATE / rate);
#if DEBUG_GAPRATE
    click_chatter("ugap: %u", _ugap);
#endif
    reset();
}

inline
GapRate::GapRate()
{
    set_rate(0);
}

inline
GapRate::GapRate(unsigned rate)
{
    set_rate(rate);
}

inline bool
GapRate::need_update(const Timestamp &now)
{
    unsigned need = (now.usec() << UGAP_SHIFT) / _ugap;

    if (_tv_sec < 0) {
	// 27.Feb.2005: often OK to send a packet after reset unless rate is
	// 0 -- requested by Bart Braem
	// check include/click/gaprate.hh (1.2)
	_tv_sec = now.sec();
	_sec_count = need + ((now.usec() << UGAP_SHIFT) - (need * _ugap) > _ugap / 2);
    } else if (now.sec() > _tv_sec) {
	_tv_sec = now.sec();
	if (_sec_count > 0)
	    _sec_count -= _rate;
    }

#if DEBUG_GAPRATE
    click_chatter("%{timestamp} -> %u @ %u [%d]", &now, need, _sec_count, (int)need >= _sec_count);
#endif
    return ((int)need >= _sec_count);
}

CLICK_ENDDECLS
#endif
