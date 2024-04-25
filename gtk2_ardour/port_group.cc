/*
 * Copyright (C) 2009-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2009-2012 David Robillard <d@drobilla.net>
 * Copyright (C) 2009-2018 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2012-2016 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2016 Julien "_FrnchFrgg_" RIVAUD <frnchfrgg@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <memory>
#include <cstring>

#include <boost/algorithm/string.hpp>

#include "midi++/mmc.h"

#include "pbd/natsort.h"

#include "ardour/audio_port.h"
#include "ardour/async_midi_port.h"
#include "ardour/audioengine.h"
#include "ardour/auditioner.h"
#include "ardour/bundle.h"
#include "ardour/control_protocol_manager.h"
#include "ardour/io_plug.h"
#include "ardour/io_processor.h"
#include "ardour/midi_port.h"
#include "ardour/midiport_manager.h"
#include "ardour/plugin_insert.h"
#include "ardour/port.h"
#include "ardour/profile.h"
#include "ardour/session.h"
#include "ardour/sidechain.h"
#include "ardour/transport_master.h"
#include "ardour/transport_master_manager.h"
#include "ardour/user_bundle.h"

#include "control_protocol/control_protocol.h"

#include "gui_thread.h"
#include "port_group.h"
#include "port_matrix.h"
#include "time_axis_view.h"
#include "public_editor.h"

#include "pbd/i18n.h"

using namespace std;
using namespace Gtk;
using namespace ARDOUR;

/** PortGroup constructor.
 * @param n Name.
 */
PortGroup::PortGroup (std::string const & n)
	: name (n)
{

}

PortGroup::~PortGroup()
{
	for (BundleList::iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		delete *i;
	}
	_bundles.clear ();
}

/** Add a bundle to a group.
 *  @param b Bundle.
 *  @param allow_dups true to allow the group to contain more than one bundle with the same port, otherwise false.
 */
void
PortGroup::add_bundle (std::shared_ptr<Bundle> b, bool allow_dups)
{
	add_bundle_internal (b, std::shared_ptr<IO> (), false, Gdk::Color (), allow_dups);
}

/** Add a bundle to a group.
 *  @param b Bundle.
 *  @param io IO whose ports are in the bundle.
 */
void
PortGroup::add_bundle (std::shared_ptr<Bundle> b, std::shared_ptr<IO> io)
{
	add_bundle_internal (b, io, false, Gdk::Color (), false);
}

/** Add a bundle to a group.
 *  @param b Bundle.
 *  @param c Colour to represent the bundle with.
 */
void
PortGroup::add_bundle (std::shared_ptr<Bundle> b, std::shared_ptr<IO> io, Gdk::Color c)
{
	add_bundle_internal (b, io, true, c, false);
}

PortGroup::BundleRecord::BundleRecord (std::shared_ptr<ARDOUR::Bundle> b, std::shared_ptr<ARDOUR::IO> iop, Gdk::Color c, bool has_c)
	: bundle (b)
	, io (iop)
	, colour (c)
	, has_colour (has_c)
{
}

void
PortGroup::add_bundle_internal (std::shared_ptr<Bundle> b, std::shared_ptr<IO> io, bool has_colour, Gdk::Color colour, bool allow_dups)
{
	assert (b.get());

	if (!allow_dups) {

		/* don't add this bundle if we already have one with the same ports */

		BundleList::iterator i = _bundles.begin ();
		while (i != _bundles.end() && b->has_same_ports ((*i)->bundle) == false) {
			++i;
		}

		if (i != _bundles.end ()) {
			return;
		}
	}

	BundleRecord* br = new BundleRecord (b, io, colour, has_colour);
	b->Changed.connect (br->changed_connection, invalidator (*this), boost::bind (&PortGroup::bundle_changed, this, _1), gui_context());
	_bundles.push_back (br);

	Changed ();
}

void
PortGroup::remove_bundle (std::shared_ptr<Bundle> b)
{
	assert (b.get());

	BundleList::iterator i = _bundles.begin ();
	while (i != _bundles.end() && (*i)->bundle != b) {
		++i;
	}

	if (i == _bundles.end()) {
		return;
	}

	delete *i;
	_bundles.erase (i);

	Changed ();
}

void
PortGroup::bundle_changed (Bundle::Change c)
{
	BundleChanged (c);
}


void
PortGroup::clear ()
{
	for (BundleList::iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		delete *i;
	}

	_bundles.clear ();
	Changed ();
}

bool
PortGroup::has_port (std::string const& p) const
{
	for (BundleList::const_iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		if ((*i)->bundle->offers_port_alone (p)) {
			return true;
		}
	}

	return false;
}

std::shared_ptr<Bundle>
PortGroup::only_bundle ()
{
	assert (_bundles.size() == 1);
	return _bundles.front()->bundle;
}


ChanCount
PortGroup::total_channels () const
{
	ChanCount n;
	for (BundleList::const_iterator i = _bundles.begin(); i != _bundles.end(); ++i) {
		n += (*i)->bundle->nchannels ();
	}

	return n;
}

std::shared_ptr<IO>
PortGroup::io_from_bundle (std::shared_ptr<ARDOUR::Bundle> b) const
{
	BundleList::const_iterator i = _bundles.begin ();
	while (i != _bundles.end() && (*i)->bundle != b) {
		++i;
	}

	if (i == _bundles.end()) {
		return std::shared_ptr<IO> ();
	}

	std::shared_ptr<IO> io ((*i)->io.lock ());
	return io;
}

/** Remove bundles whose channels are already represented by other, larger bundles */
void
PortGroup::remove_duplicates ()
{
	BundleList::iterator i = _bundles.begin();
	while (i != _bundles.end()) {

		BundleList::iterator tmp = i;
		++tmp;

		bool remove = false;

		for (BundleList::iterator j = _bundles.begin(); j != _bundles.end(); ++j) {

			if ((*j)->bundle->nchannels() > (*i)->bundle->nchannels()) {
				/* this bundle is larger */

				uint32_t k = 0;
				while (k < (*i)->bundle->nchannels().n_total()) {
					/* see if this channel on *i has an equivalent on *j */
					uint32_t l = 0;
					while (l < (*j)->bundle->nchannels().n_total() && (*i)->bundle->channel_ports (k) != (*j)->bundle->channel_ports (l)) {
						++l;
					}

					if (l == (*j)->bundle->nchannels().n_total()) {
						/* it does not */
						break;
					}

					++k;
				}

				if (k == (*i)->bundle->nchannels().n_total()) {
					/* all channels on *i are represented by the larger bundle *j, so remove *i */
					remove = true;
					break;
				}
			}
		}

		if (remove) {
			_bundles.erase (i);
		}

		i = tmp;
	}
}


/** PortGroupList constructor.
 */
PortGroupList::PortGroupList ()
	: _signals_suspended (false), _pending_change (false), _pending_bundle_change ((Bundle::Change) 0)
{

}

PortGroupList::~PortGroupList()
{
	/* XXX need to clean up bundles, but ownership shared with PortGroups */
}

void
PortGroupList::maybe_add_processor_to_list (
	std::weak_ptr<Processor> wp, list<std::shared_ptr<IO> >* route_ios, bool inputs, set<std::shared_ptr<IO> >& used_io
	)
{
	std::shared_ptr<Processor> p (wp.lock());

	if (!p) {
		return;
	}

	std::shared_ptr<IOProcessor> iop = std::dynamic_pointer_cast<IOProcessor> (p);

	if (iop) {

		std::shared_ptr<IO> io = inputs ? iop->input() : iop->output();

		if (io && used_io.find (io) == used_io.end()) {
			route_ios->push_back (io);
			used_io.insert (io);
		}
	}
}

struct RouteIOs {
	RouteIOs (std::shared_ptr<Route> r, std::shared_ptr<IO> i) {
		route = r;
		ios.push_back (i);
	}

	std::shared_ptr<Route> route;
	/* it's ok to use a shared_ptr here as RouteIOs structs are only used during ::gather () */
	std::list<std::shared_ptr<IO> > ios;
};

class RouteIOsComparator {
public:
	bool operator() (RouteIOs const & a, RouteIOs const & b) {
		return a.route->presentation_info ().order() < b.route->presentation_info().order();
	}
};

/** Gather ports from around the system and put them in this PortGroupList.
 *  @param type Type of ports to collect, or NIL for all types.
 *  @param use_session_bundles true to use the session's non-user bundles.  Doing this will mean that
 *  hardware ports will be gathered into stereo pairs, as the session sets up bundles for these pairs.
 *  Not using the session bundles will mean that all hardware IO will be presented separately.
 */
void
PortGroupList::gather (ARDOUR::Session* session, ARDOUR::DataType type, bool inputs, bool allow_dups, bool use_session_bundles)
{
	clear ();

	if (session == 0) {
		return;
	}

	std::shared_ptr<PortGroup> bus (new PortGroup (_("Busses")));
	std::shared_ptr<PortGroup> track (new PortGroup (_("Tracks")));
	std::shared_ptr<PortGroup> sidechain (new PortGroup (_("Sidechains")));
	std::shared_ptr<PortGroup> iop_pre  (new PortGroup (_("I/O Pre") ));
	std::shared_ptr<PortGroup> iop_post (new PortGroup (_("I/O Post") ));
	std::shared_ptr<PortGroup> system (new PortGroup (_("Hardware")));
	std::shared_ptr<PortGroup> program (new PortGroup (string_compose (_("%1 Misc"), PROGRAM_NAME)));
	std::shared_ptr<PortGroup> other (new PortGroup (_("External")));

	/* Find the IOs which have bundles for routes and their processors.  We store
	   these IOs in a RouteIOs class so that we can then sort the results by route
	   order key.
	*/

	std::shared_ptr<RouteList const> routes = session->get_routes ();
	list<RouteIOs> route_ios;

	for (RouteList::const_iterator i = routes->begin(); i != routes->end(); ++i) {

		/* we never show the monitor bus inputs */

		if (inputs && (*i)->is_monitor()) {
			continue;
		}

		/* keep track of IOs that we have taken bundles from,
		   so that we can avoid taking the same IO from both
		   Route::output() and the main_outs Delivery
                */

		set<std::shared_ptr<IO> > used_io;
		std::shared_ptr<IO> io = inputs ? (*i)->input() : (*i)->output();
		used_io.insert (io);

		RouteIOs rb (*i, io);
		(*i)->foreach_processor (boost::bind (&PortGroupList::maybe_add_processor_to_list, this, _1, &rb.ios, inputs, used_io));

		route_ios.push_back (rb);
	}

	/* Sort RouteIOs by the routes' editor order keys */
	route_ios.sort (RouteIOsComparator ());

	/* Now put the bundles that belong to these sorted RouteIOs into the PortGroup. */

	for (list<RouteIOs>::iterator i = route_ios.begin(); i != route_ios.end(); ++i) {
		TimeAxisView* tv = PublicEditor::instance().time_axis_view_from_stripable (i->route);

		/* Work out which group to put these IOs' bundles in */
		std::shared_ptr<PortGroup> g;
		if (std::dynamic_pointer_cast<Track> (i->route)) {
			g = track;
		} else {
			g = bus;
		}

		for (list<std::shared_ptr<IO> >::iterator j = i->ios.begin(); j != i->ios.end(); ++j) {
			/* Only add the bundle if there is at least one port
			 * with a type that's been asked for */
			if (type == DataType::NIL || (*j)->bundle()->nchannels().n(type) > 0) {
				if (tv) {
					g->add_bundle ((*j)->bundle(), *j, tv->color ());
				} else {
					g->add_bundle ((*j)->bundle(), *j);
				}
			}
		}

		/* When on input side, let's look for sidechains in the route's plugins
		   to display them right next to their route */
		for (uint32_t n = 0; inputs; ++n) {
			std::shared_ptr<Processor> p = (i->route)->nth_plugin (n);
			if (!p) {
				break;
			}
			std::shared_ptr<SideChain> sc = std::static_pointer_cast<PluginInsert> (p)->sidechain ();

			if (sc) {
				std::shared_ptr<IO> io = sc->input();
				if (tv) {
					sidechain->add_bundle (io->bundle(), io, tv->color ());
				} else {
					sidechain->add_bundle (io->bundle(), io);
				}
			}
		}
	}

	/* Bundles owned by the session; add user bundles first, then normal ones, so
	   that UserBundles that offer the same ports as a normal bundle get priority
	*/

	std::shared_ptr<BundleList const> b = session->bundles ();

	for (auto const& i : *b) {
		if (std::dynamic_pointer_cast<UserBundle> (i) && i->ports_are_inputs() == inputs) {
			system->add_bundle (i, allow_dups);
		}
	}

	/* Only look for non-user bundles if instructed to do so */
	if (use_session_bundles) {
		for (auto const& i : *b) {
			if (std::dynamic_pointer_cast<UserBundle> (i) == 0 && i->ports_are_inputs() == inputs) {
				system->add_bundle (i, allow_dups);
			}
		}
	}

	/* miscellany */

	if (type == DataType::AUDIO || type == DataType::NIL) {
		if (!inputs) {

			if (session->the_auditioner()) {
				program->add_bundle (session->the_auditioner()->output()->bundle());
			}
			if (session->click_io()) {
				program->add_bundle (session->click_io()->bundle());
			}

			std::shared_ptr<Bundle> ltc (new Bundle (_("LTC Out"), inputs));
			ltc->add_channel (_("LTC Out"), DataType::AUDIO, session->engine().make_port_name_non_relative (session->ltc_output_port()->name()));
			program->add_bundle (ltc);

		} else {

			std::shared_ptr<Bundle> sync (new Bundle (_("Sync"), inputs));
			AudioEngine* ae = AudioEngine::instance();
			TransportMasterManager::TransportMasters const & tm (TransportMasterManager::instance().transport_masters());

			for (TransportMasterManager::TransportMasters::const_iterator i = tm.begin(); i != tm.end(); ++i) {

				std::shared_ptr<Port> port = (*i)->port ();

				if (!port) {
					continue;
				}

				if (!std::dynamic_pointer_cast<AudioPort> (port)) {
					continue;
				}

				sync->add_channel ((*i)->name(), DataType::AUDIO, ae->make_port_name_non_relative (port->name()));
			}

			program->add_bundle (sync);
		}
	}

	/* our control surfaces */

	/* XXX assume for now that all control protocols with ports use
	 * MIDI. If anyone created a control protocol that used audio ports,
	 * this will break.
	 */

	if ((type == DataType::MIDI || type == DataType::NIL)) {
		ControlProtocolManager& m = ControlProtocolManager::instance ();
		for (list<ControlProtocolInfo*>::iterator i = m.control_protocol_info.begin(); i != m.control_protocol_info.end(); ++i) {
			if ((*i)->protocol) {
				list<std::shared_ptr<Bundle> > b = (*i)->protocol->bundles ();
				for (list<std::shared_ptr<Bundle> >::iterator j = b.begin(); j != b.end(); ++j) {
					if ((*j)->ports_are_inputs() == inputs) {
						program->add_bundle (*j);
					}
				}
			}
		}
	}

	/* virtual keyboard */
	if (!inputs && (type == DataType::MIDI || type == DataType::NIL)) {
		std::shared_ptr<ARDOUR::Port> ap = std::dynamic_pointer_cast<ARDOUR::Port> (session->vkbd_output_port());
		AudioEngine* ae = AudioEngine::instance();
		std::shared_ptr<Bundle> vm (new Bundle (ap->pretty_name (), inputs));
		vm->add_channel (ap->pretty_name (), DataType::MIDI, ae->make_port_name_non_relative (ap->name()));
		program->add_bundle (vm);
	}

	/* our sync ports */

	if ((type == DataType::MIDI || type == DataType::NIL)) {
		std::shared_ptr<Bundle> sync (new Bundle (_("Sync"), inputs));
		AudioEngine* ae = AudioEngine::instance();
		TransportMasterManager::TransportMasters const & tm (TransportMasterManager::instance().transport_masters());

		if (inputs) {

			for (TransportMasterManager::TransportMasters::const_iterator i = tm.begin(); i != tm.end(); ++i) {
				std::shared_ptr<Port> port = (*i)->port ();
				if (!port) {
					continue;
				}

				if (!std::dynamic_pointer_cast<MidiPort> (port)) {
					continue;
				}

				sync->add_channel ((*i)->name(), DataType::MIDI, ae->make_port_name_non_relative (port->name()));
			}

			sync->add_channel (_("MMC in"), DataType::MIDI, ae->make_port_name_non_relative (session->mmc_input_port()->name()));

		} else {

			sync->add_channel (
				_("MTC out"), DataType::MIDI, ae->make_port_name_non_relative (session->mtc_output_port()->name())
				);
			sync->add_channel (
				_("MIDI clock out"), DataType::MIDI, ae->make_port_name_non_relative (session->midi_clock_output_port()->name())
				);
			sync->add_channel (
				_("MMC out"), DataType::MIDI, ae->make_port_name_non_relative (session->mmc_output_port()->name())
				);
		}

		program->add_bundle (sync);
	}

	for (auto const& iop : *session->io_plugs ()) {
		std::shared_ptr<IO> io = inputs ? iop->input() : iop->output();
		if (io->n_ports().n_total () == 0) {
			continue;
		}
		if (type == DataType::NIL || io->n_ports().get (type) > 0) {
			if (iop->is_pre ()) {
				iop_pre->add_bundle (io->bundle(), io);
			} else {
				iop_post->add_bundle (io->bundle(), io);
			}
		}
	}

	/* Now find all other ports that we haven't thought of yet */

	std::vector<std::string> extra_system[DataType::num_types];
	std::vector<std::string> extra_program[DataType::num_types];
	std::vector<std::string> extra_other[DataType::num_types];

	string lpn (PROGRAM_NAME);
	boost::to_lower (lpn);
	string lpnc = lpn;
	lpnc += ':';

	vector<string> ports;
	if (type == DataType::NIL) {
		vector<string> p1;
		AudioEngine::instance()->get_ports ("", DataType::AUDIO, inputs ? IsInput : IsOutput, ports);
		AudioEngine::instance()->get_ports ("", DataType::MIDI, inputs ? IsInput : IsOutput, p1);
		for (vector<string>::const_iterator s = p1.begin(); s != p1.end(); ++s) {
			ports.push_back (*s);
		}
	} else {
		AudioEngine::instance()->get_ports ("", type, inputs ? IsInput : IsOutput, ports);
	}

	if (ports.size () > 0) {

		struct SortByPortName {
			bool operator() (std::string const& lhs, std::string const& rhs) const {
				return PBD::naturally_less (lhs.c_str (), rhs.c_str ());
			}
		} port_sorter;

		std::sort (ports.begin (), ports.end (), port_sorter);

		for (vector<string>::const_iterator s = ports.begin(); s != ports.end(); ++s) {

			std::string const p = *s;

			if (allow_dups || (
			        !system->has_port(p)
			     && !bus->has_port(p)
			     && !track->has_port(p)
			     && !iop_pre->has_port(p)
			     && !iop_post->has_port(p)
			     && !sidechain->has_port(p)
			     && !program->has_port(p)
			     && !other->has_port(p)
			    )
			   ) {

				/* special hack: ignore MIDI ports labelled Midi-Through. these
				   are basically useless and mess things up for default
				   connections.
				*/

				if (p.find ("Midi-Through") != string::npos || p.find ("Midi Through") != string::npos) {
					continue;
				}

				/* special hack: ignore our monitor inputs (which show up here because
				   we excluded them earlier.
				*/

				string lp = p;
				string monitor = _("Monitor");

				boost::to_lower (lp);
				boost::to_lower (monitor);

				if ((lp.find (monitor) != string::npos) &&
				    (lp.find (lpn) != string::npos)) {
					continue;
				}

				/* can't use the audio engine for this as we
				 * are looking at ports not owned by the
				 * application, and the audio engine/port
				 * manager doesn't seem them.
				 */

				PortEngine::PortHandle ph = AudioEngine::instance()->port_engine().get_port_by_name (p);

				if (!ph) {
					continue;
				}

				DataType t (AudioEngine::instance()->port_engine().port_data_type (ph));

				if (t != DataType::NIL) {

					PortFlags flags (AudioEngine::instance()->port_engine().get_port_flags (ph));

					if (flags & Hidden ) {
						continue;
					} else if (port_has_prefix (p, lpnc)) {

						/* we own this port (named after the program) */

						extra_program[t].push_back (p);

					} else if (flags & IsPhysical) {

						extra_system[t].push_back (p);

					} else {
						extra_other[t].push_back (p);
					}
				}
			}
		}
	}

	for (DataType::iterator i = DataType::begin(); i != DataType::end(); ++i) {
		if (!extra_system[*i].empty()) {
			add_bundles_for_ports (extra_system[*i], *i, inputs, allow_dups, system);
		}
	}

	for (DataType::iterator i = DataType::begin(); i != DataType::end(); ++i) {
		if (!extra_program[*i].empty()) {
			/* used program name as bundle name */
			std::shared_ptr<Bundle> b = make_bundle_from_ports (extra_program[*i], *i, inputs, lpn);
			program->add_bundle (b);
		}
	}

	for (DataType::iterator i = DataType::begin(); i != DataType::end(); ++i) {
		if (extra_other[*i].empty()) continue;
		add_bundles_for_ports (extra_other[*i], *i, inputs, allow_dups, other);
	}

	if (!allow_dups) {
		system->remove_duplicates ();
	}

	add_group_if_not_empty (bus);
	add_group_if_not_empty (track);
	add_group_if_not_empty (sidechain);
	add_group_if_not_empty (iop_pre);
	add_group_if_not_empty (iop_post);
	add_group_if_not_empty (program);
	add_group_if_not_empty (other);
	add_group_if_not_empty (system);

	emit_changed ();
}

void
PortGroupList::add_bundles_for_ports (std::vector<std::string> const & p, ARDOUR::DataType type, bool inputs, bool allow_dups, std::shared_ptr<PortGroup> group) const
{
	bool has_colon = true, has_slash = true;
	for (const auto& s : p) {
		if (s.find('/') == std::string::npos) has_slash = false;
		if (s.find(':') == std::string::npos) has_colon = false;
	}
	std::string sep = has_slash ? "/" : has_colon ? ":" : "";
	if (sep.empty()) {
		std::shared_ptr<Bundle> b = make_bundle_from_ports (p, type, inputs);
		group->add_bundle (b, allow_dups);
		return;
	}

	std::vector<std::string> nb;
	std::string cp;
	for (const auto& s : p) {
		std::string pf = s.substr (0, s.find_first_of (sep) + 1);
		if (pf != cp && !nb.empty()) {
				std::shared_ptr<Bundle> b = make_bundle_from_ports (nb, type, inputs);
				group->add_bundle (b, allow_dups);
				nb.clear();			
		}
		cp = pf;
		nb.push_back(s);
	}
	if (!nb.empty()) {
		std::shared_ptr<Bundle> b = make_bundle_from_ports (nb, type, inputs);
		group->add_bundle (b, allow_dups);
	}
}

std::shared_ptr<Bundle>
PortGroupList::make_bundle_from_ports (std::vector<std::string> const & p, ARDOUR::DataType type, bool inputs, std::string const& bundle_name) const
{
	std::shared_ptr<Bundle> b (new Bundle ("", inputs));
	std::string const pre = common_prefix (p);

	if (!bundle_name.empty()) {
		b->set_name (bundle_name);
	} else {
		if (!pre.empty()) {
			b->set_name (pre.substr (0, pre.length() - 1));
		}
	}

	for (uint32_t j = 0; j < p.size(); ++j) {
		std::string n = p[j].substr (pre.length());
		std::string pn = AudioEngine::instance()->get_pretty_name_by_name (p[j]);
		if (!pn.empty()) {
			n = pn;
		}
		b->add_channel (n, type);
		b->set_port (j, p[j]);
	}

	return b;
}

bool
PortGroupList::port_has_prefix (const std::string& n, const std::string& p) const
{
	return n.substr (0, p.length()) == p;
}

std::string
PortGroupList::common_prefix_before (std::vector<std::string> const & p, std::string const & s) const
{
	/* we must have some strings and the first must contain the separator string */
	if (p.empty() || p[0].find_first_of (s) == std::string::npos) {
		return "";
	}

	/* prefix of the first string */
	std::string const fp = p[0].substr (0, p[0].find_first_of (s) + 1);

	/* see if the other strings also start with fp */
	uint32_t j = 1;
	while (j < p.size()) {
		if (p[j].substr (0, fp.length()) != fp) {
			break;
		}
		++j;
	}

	if (j != p.size()) {
		return "";
	}

	return fp;
}


std::string
PortGroupList::common_prefix (std::vector<std::string> const & p) const
{
	/* common prefix before '/' ? */
	std::string cp = common_prefix_before (p, "/");
	if (!cp.empty()) {
		return cp;
	}

	cp = common_prefix_before (p, ":");
	if (!cp.empty()) {
		return cp;
	}

	return "";
}

void
PortGroupList::clear ()
{
	_groups.clear ();
	_bundle_changed_connections.drop_connections ();
	emit_changed ();
}


PortGroup::BundleList const &
PortGroupList::bundles () const
{
	_bundles.clear ();

	for (PortGroupList::List::const_iterator i = begin (); i != end (); ++i) {
		std::copy ((*i)->bundles().begin(), (*i)->bundles().end(), std::back_inserter (_bundles));
	}

	return _bundles;
}

ChanCount
PortGroupList::total_channels () const
{
	ChanCount n;

	for (PortGroupList::List::const_iterator i = begin(); i != end(); ++i) {
		n += (*i)->total_channels ();
	}

	return n;
}

void
PortGroupList::add_group_if_not_empty (std::shared_ptr<PortGroup> g)
{
	if (!g->bundles().empty ()) {
		add_group (g);
	}
}

void
PortGroupList::add_group (std::shared_ptr<PortGroup> g)
{
	_groups.push_back (g);

	g->Changed.connect (_changed_connections, invalidator (*this), boost::bind (&PortGroupList::emit_changed, this), gui_context());
	g->BundleChanged.connect (_bundle_changed_connections, invalidator (*this), boost::bind (&PortGroupList::emit_bundle_changed, this, _1), gui_context());

	emit_changed ();
}

void
PortGroupList::remove_bundle (std::shared_ptr<Bundle> b)
{
	for (List::iterator i = _groups.begin(); i != _groups.end(); ++i) {
		(*i)->remove_bundle (b);
	}

	emit_changed ();
}

void
PortGroupList::emit_changed ()
{
	if (_signals_suspended) {
		_pending_change = true;
	} else {
		Changed ();
	}
}

void
PortGroupList::emit_bundle_changed (Bundle::Change c)
{
	if (_signals_suspended) {
		_pending_bundle_change = c;
	} else {
		BundleChanged (c);
	}
}
void
PortGroupList::suspend_signals ()
{
	_signals_suspended = true;
}

void
PortGroupList::resume_signals ()
{
	if (_pending_change) {
		Changed ();
		_pending_change = false;
	}

	if (_pending_bundle_change != 0) {
		BundleChanged (_pending_bundle_change);
		_pending_bundle_change = (ARDOUR::Bundle::Change) 0;
	}

	_signals_suspended = false;
}

std::shared_ptr<IO>
PortGroupList::io_from_bundle (std::shared_ptr<ARDOUR::Bundle> b) const
{
	List::const_iterator i = _groups.begin ();
	while (i != _groups.end()) {
		std::shared_ptr<IO> io = (*i)->io_from_bundle (b);
		if (io) {
			return io;
		}
		++i;
	}

	return std::shared_ptr<IO> ();
}

bool
PortGroupList::empty () const
{
	return _groups.empty ();
}
