/*
 * click-xform.cc -- pattern-based optimizer for Click configurations
 * Eddie Kohler
 *
 * Copyright (c) 1999 Massachusetts Institute of Technology.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "routert.hh"
#include "lexert.hh"
#include "error.hh"
#include "confparse.hh"
#include "clp.h"
#include "toolutils.hh"
#include <stdio.h>
#include <ctype.h>

bool match_config(const String &, const String &, HashMap<String, String> &);
// TODO: allow some special pports to be unconnected

class Matcher {

  RouterT *_pat;
  RouterT *_body;
  int _patid;

  int _pat_input_idx;
  int _pat_output_idx;
  
  Vector<int> _match;
  Vector<int> _back_match;
  HashMap<String, String> _defs;

  Vector<Hookup> _to_pp_from;
  Vector<Hookup> _to_pp_to;
  Vector<Hookup> _from_pp_from;
  Vector<Hookup> _from_pp_to;

 public:

  Matcher(RouterT *, RouterT *, int, ErrorHandler *);
  ~Matcher();

  bool check_into(const Hookup &, const Hookup &);
  bool check_out_of(const Hookup &, const Hookup &);

  bool check_match();
  bool next_match();

  void replace_config(String &) const;
  void replace(RouterT *, const String &, const String &, ErrorHandler *);
  
};


Matcher::Matcher(RouterT *pat, RouterT *body, int patid, ErrorHandler *errh)
  : _pat(pat), _body(body), _patid(patid),
    _pat_input_idx(-1), _pat_output_idx(-1)
{
  _pat->use();
  _body->use();
  // check tunnel situation
  for (int i = 0; i < _pat->nelements(); i++) {
    ElementT &fac = _pat->element(i);
    if (fac.type != RouterT::TUNNEL_TYPE)
      continue;
    else if (fac.tunnel_input >= 0 || fac.tunnel_output >= 0)
      errh->lerror(fac.landmark, "pattern has active connection tunnels");
    else if (fac.name == "input" && _pat_input_idx < 0)
      _pat_input_idx = i;
    else if (fac.name == "output" && _pat_output_idx < 0)
      _pat_output_idx = i;
    else
      errh->lerror(fac.landmark, "connection tunnel with funny name `%s'", fac.name.cc());
  }
}

Matcher::~Matcher()
{
  _pat->unuse();
  _body->unuse();
}

bool
Matcher::check_into(const Hookup &houtside, const Hookup &hinside)
{
  const Vector<Hookup> &phf = _pat->hookup_from();
  const Vector<Hookup> &pht = _pat->hookup_to();
  Hookup phinside(_back_match[hinside.idx], hinside.port);
  Hookup success(_pat->nelements(), 0);
  // now look for matches
  for (int i = 0; i < phf.size(); i++)
    if (pht[i] == phinside && phf[i].idx == _pat_input_idx
	&& phf[i] < success) {
      Vector<Hookup> pfrom_phf, from_outside;
      // check that it's valid: all connections from tunnels are covered
      // in body
      _pat->find_connections_from(phf[i], pfrom_phf);
      _body->find_connections_from(houtside, from_outside);
      for (int j = 0; j < pfrom_phf.size(); j++) {
	Hookup want(_match[pfrom_phf[j].idx], pfrom_phf[j].port);
	if (want.index_in(from_outside) < 0)
	  goto no_match;
      }
      // success: save it
      success = phf[i];
     no_match: ;
    }
  // if succeeded, save it
  if (success.idx < _pat->nelements()) {
    _to_pp_from.push_back(houtside);
    _to_pp_to.push_back(success);
    return true;
  } else
    return false;
}

bool
Matcher::check_out_of(const Hookup &hinside, const Hookup &houtside)
{
  const Vector<Hookup> &phf = _pat->hookup_from();
  const Vector<Hookup> &pht = _pat->hookup_to();
  Hookup phinside(_back_match[hinside.idx], hinside.port);
  Hookup success(_pat->nelements(), 0);
  // now look for matches
  for (int i = 0; i < phf.size(); i++)
    if (phf[i] == phinside && pht[i].idx == _pat_output_idx
	&& pht[i] < success) {
      Vector<Hookup> pto_pht, to_outside;
      // check that it's valid: all connections to tunnels are covered
      // in body
      _pat->find_connections_to(pht[i], pto_pht);
      _body->find_connections_to(houtside, to_outside);
      for (int j = 0; j < pto_pht.size(); j++) {
	Hookup want(_match[pto_pht[j].idx], pto_pht[j].port);
	if (want.index_in(to_outside) < 0)
	  goto no_match;
      }
      // success: save it
      success = pht[i];
     no_match: ;
    }
  // if succeeded, save it
  if (success.idx < _pat->nelements()) {
    _from_pp_from.push_back(success);
    _from_pp_to.push_back(houtside);
    return true;
  } else
    return false;
}

bool
Matcher::check_match()
{
  _from_pp_from.clear();
  _from_pp_to.clear();
  _to_pp_from.clear();
  _to_pp_to.clear();
  _defs.clear();

  // check configurations
  for (int i = 0; i < _match.size(); i++)
    if (_match[i] >= 0) {
      if (!match_config(_pat->econfiguration(i), _body->econfiguration(_match[i]), _defs))
	return false;
    }
  
  int bnf = _body->nelements();
  _back_match.assign(bnf, -1);
  bool all_previous_match = true;
  for (int i = 0; i < _match.size(); i++) {
    int j = _match[i];
    if (j >= 0) {
      _back_match[j] = i;
      if (_body->eflags(j) != _patid)	// comes from replacement of same pat
	all_previous_match = false;
    }
  }
  // don't allow match if it consists entirely of elements previously replaced
  // by this pattern
  if (all_previous_match)
    return false;

  // find the pattern ports any cross-pattern jumps correspond to
  const Vector<Hookup> &hfrom = _body->hookup_from();
  const Vector<Hookup> &hto = _body->hookup_to();
  int nhook = hfrom.size();

  for (int i = 0; i < nhook; i++) {
    const Hookup &hf = hfrom[i], &ht = hto[i];
    int pf = _back_match[hf.idx], pt = _back_match[ht.idx];
    if (pf >= 0 && pt >= 0) {
      if (!_pat->has_connection(Hookup(pf, hf.port), Hookup(pt, ht.port)))
	return false;
    } else if (pf < 0 && pt >= 0) {
      if (!check_into(hf, ht))
	return false;
    } else if (pf >= 0 && pt < 0) {
      if (!check_out_of(hf, ht))
	return false;
    }
  }

  // check for unconnected tunnels in the pattern
  const Vector<Hookup> &phf = _pat->hookup_from();
  const Vector<Hookup> &pht = _pat->hookup_to();
  for (int i = 0; i < phf.size(); i++) {
    if (phf[i].idx == _pat_input_idx && phf[i].index_in(_to_pp_to) < 0)
      return false;
    if (pht[i].idx == _pat_output_idx && pht[i].index_in(_from_pp_from) < 0)
      return false;
  }
  
  return true;
}

bool
Matcher::next_match()
{
  while (_body->next_connection_match(_pat, _match)) {
    if (check_match())
      return true;
  }
  return false;
}

static String
uniqueify_prefix(const String &base_prefix, RouterT *r)
{
  int count = 1;
  while (1) {
    String prefix = base_prefix;
    prefix += "@" + String(count);
    count++;
    
    // look for things starting with that name
    int plen = prefix.length();
    for (int i = 0; i < r->nelements(); i++) {
      const String &n = r->ename(i);
      if (n.length() > plen + 1 && n.substring(0, plen) == prefix
	  && n[plen] == '/')
	goto failed;
    }

    return prefix;

   failed: ;
  }
}

void
Matcher::replace_config(String &configuration) const
{
  Vector<String> confvec;
  cp_argvec(configuration, confvec);
  
  bool changed = false;
  for (int i = 0; i < confvec.size(); i++) {
    if (confvec[i].length() <= 1 || confvec[i][0] != '$')
      continue;
    if (String *vp = _defs.findp(confvec[i])) {
      confvec[i] = *vp;
      changed = true;
    }
  }

  if (changed)
    configuration = cp_unargvec(confvec);
}

void
Matcher::replace(RouterT *replacement, const String &try_prefix,
		 const String &landmark, ErrorHandler *errh)
{
  //fprintf(stderr, "replace...\n");
  String prefix = uniqueify_prefix(try_prefix, _body);
  
  // add replacement
  int old_nelements = _body->nelements();
  ElementT fake_for_compound(prefix, -1, String(), landmark);
  replacement->expand_compound(fake_for_compound, _body, errh);

  // mark replacement
  for (int i = old_nelements; i < _body->nelements(); i++) {
    _body->element(i).flags = _patid;
    replace_config(_body->econfiguration(i));
  }

  // find input and output, add connections to tunnels
  int new_pp = _body->eindex(prefix);
  for (int i = 0; i < _to_pp_from.size(); i++)
    _body->add_connection(_to_pp_from[i], Hookup(new_pp, _to_pp_to[i].port),
			  landmark);
  for (int i = 0; i < _from_pp_from.size(); i++)
    _body->add_connection(Hookup(new_pp, _from_pp_from[i].port),
			  _from_pp_to[i], landmark);
  
  // remove match
  for (int i = 0; i < old_nelements; i++)
    if (_back_match[i] >= 0)
      _body->element(i).type = -1;
  _body->remove_blank_elements();

  // cleanup
  _body->flatten(errh);
  _match.clear();
}


// match configuration strings
bool
match_config(const String &pat, const String &conf,
	     HashMap<String, String> &defs)
{
  Vector<String> patvec, confvec;
  HashMap<String, String> my_defs;

  // separate into vectors
  cp_argvec(pat, patvec);
  cp_argvec(conf, confvec);

  // not same size -> no match
  if (patvec.size() != confvec.size())
    return false;

  // go over elements and compare; handle $variables
  for (int i = 0; i < patvec.size(); i++) {
    if (patvec[i] == confvec[i])
      continue;
    if (patvec[i].length() <= 1 || patvec[i][0] != '$')
      return false;
    const String &p = patvec[i];
    for (int j = 1; j < p.length(); j++)
      if (!isalnum(p[j]) && p[j] != '_')
	return false;
    if (String *dp = defs.findp(p)) {
      if (*dp != confvec[i])
	return false;
    } else if (String *mp = my_defs.findp(p)) {
      if (*mp != confvec[i])
	return false;
    } else
      my_defs.insert(p, confvec[i]);
  }

  // insert my defs into defs
  int i;
  String p, v;
  for (i = 0; my_defs.each(i, p, v); )
    defs.insert(p, v);
  
  return true;
}


#define HELP_OPT		300
#define VERSION_OPT		301
#define ROUTER_OPT		302
#define PATTERNS_OPT		303
#define OUTPUT_OPT		304

static Clp_Option options[] = {
  { "file", 'f', ROUTER_OPT, Clp_ArgString, 0 },
  { "help", 0, HELP_OPT, 0, 0 },
  { "output", 'o', OUTPUT_OPT, Clp_ArgString, 0 },
  { "patterns", 'p', PATTERNS_OPT, Clp_ArgString, 0 },
  { "version", 'v', VERSION_OPT, 0, 0 },
};

static const char *program_name;

void
short_usage()
{
  fprintf(stderr, "Usage: %s [OPTION]... [ROUTERFILE] [PATTERNFILE]...\n\
Try `%s --help' for more information.\n",
	  program_name, program_name);
}

void
usage()
{
  printf("\
`Click-xform' replaces patterns of elements with other sets of elements inside\n\
a Click router configuration. Both patterns and configuration are Click-\n\
language files. The transformed router configuration is written to the\n\
standard output.\n\
\n\
Usage: %s [OPTION]... [ROUTERFILE] [PATTERNFILE]...\n\
\n\
Options:\n\
  -p, --patterns PATTERNFILE    Read patterns from PATTERNFILE. Can be given\n\
                                more than once.\n\
  -f, --file FILE               Read router configuration from FILE.\n\
  -o, --output FILE             Write output to FILE.\n\
      --help                    Print this message and exit.\n\
  -v, --version                 Print version number and exit.\n\
\n\
Report bugs to <click@pdos.lcs.mit.edu>.\n", program_name);
}

static Vector<RouterT *> patterns;
static Vector<RouterT *> replacements;
static Vector<String> pat_names;
static int patterns_attempted;

void
read_pattern_file(const char *name, ErrorHandler *errh)
{
  patterns_attempted++;
  RouterT *pat_file = read_router_file(name, errh);
  if (!pat_file) return;
  
  for (int i = 0; i < pat_file->ntypes(); i++) {
    ElementClassT *fclass = pat_file->type_class(i);
    String name = pat_file->type_name(i);
    if (fclass && name.length() > 12
	&& name.substring(-12) == "_Replacement") {
      int ti = pat_file->type_index(name.substring(0, -12));
      if (ti >= 0 && pat_file->type_class(ti)) {
	RouterT *rep = fclass->cast_router();
	RouterT *pat = pat_file->type_class(ti)->cast_router();
	if (rep && pat) {
	  pat->flatten(errh);
	  patterns.push_back(pat);
	  replacements.push_back(rep);
	  pat_names.push_back(name.substring(0, -12));
	}
      }
    }
  }
}

int
main(int argc, char **argv)
{
  String::static_initialize();
  ErrorHandler::static_initialize(new FileErrorHandler(stderr));
  ErrorHandler *errh = ErrorHandler::default_handler();

  // read command line arguments
  Clp_Parser *clp =
    Clp_NewParser(argc, argv, sizeof(options) / sizeof(options[0]), options);
  program_name = Clp_ProgramName(clp);

  int num_nondash_args = 0;
  const char *router_file = 0;
  const char *output_file = 0;
  
  while (1) {
    int opt = Clp_Next(clp);
    switch (opt) {
      
     case HELP_OPT:
      usage();
      exit(0);
      break;
      
     case VERSION_OPT:
      printf("click-xform (Click) %s\n", VERSION);
      printf("Copyright (C) 1999 Massachusetts Institute of Technology\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose.\n");
      exit(0);
      break;
      
     case PATTERNS_OPT:
      read_pattern_file(clp->arg, errh);
      break;

     case ROUTER_OPT:
      if (router_file) {
	errh->error("router file specified twice");
	goto bad_option;
      }
      router_file = clp->arg;
      break;

     case OUTPUT_OPT:
      if (output_file) {
	errh->error("output file specified twice");
	goto bad_option;
      }
      output_file = clp->arg;
      break;
      
     case Clp_NotOption:
      if (num_nondash_args == 0 && router_file) {
	errh->error("router file specified twice");
	goto bad_option;
      } else if (num_nondash_args == 0)
	router_file = clp->arg;
      else
	read_pattern_file(clp->arg, errh);
      num_nondash_args++;
      break;

     bad_option:
     case Clp_BadOption:
      short_usage();
      exit(1);
      break;
      
     case Clp_Done:
      goto done;
      
    }
  }
  
 done:
  RouterT *r = read_router_file(router_file, errh);
  if (!r || errh->nerrors() > 0)
    exit(1);
  r->flatten(errh);

  if (!patterns_attempted)
    errh->warning("no patterns read");
  
  // clear r's flags, so we know the current element complement
  // didn't come from replacements (paranoia)
  for (int i = 0; i < r->nelements(); i++)
    r->element(i).flags = 0;
  
  bool any = true;
  while (any) {
    any = false;
    for (int i = 0; i < patterns.size(); i++) {
      Matcher m(patterns[i], r, i + 1, errh);
      if (m.next_match()) {
	m.replace(replacements[i], pat_names[i], String(), errh);
	any = true;
	break;
      }
    }
  }

  // write result
  if (write_router_file(r, output_file, errh) < 0)
    exit(1);
  return 0;
}
