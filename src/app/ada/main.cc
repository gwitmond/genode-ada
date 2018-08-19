// Auto Display Adjust (ADA)

// Set two or more displays to the maximum of their common resolutions.
// or
// Set a single display to it's old resolution.

// By: Guido Witmond
// License: GPL V3+


/* Genode includes */
#include <base/signal.h>
#include <base/log.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <util/xml_node.h>
#include <base/output.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <util/xml_generator.h>


namespace Ada { struct Main;

  using namespace Genode;
}

struct Ada::Main
{
  	Genode::Env &_env;
        Genode::Attached_rom_dataspace _config_rom { _env, "connectors" };
        Genode::Heap _heap { _env.ram(), _env.rm() };

    	typedef Genode::String<100> Name;

        // Create an reverse index based on modelines.
        // For each resolution, record which connectors offer it.

        struct Connector : Genode::List<Connector>::Element {
	  Name      name;
	  unsigned  hz;

	  Connector(Name n, unsigned h) {
	    name = n;
	    hz = h;
	  };
	};

        struct Mode : Genode::List<Mode>::Element {
	  unsigned  width;
	  unsigned  height;
	  Genode::List<Connector> connectors; // the connectors that offer this resolution

	  Mode(unsigned w, unsigned h) {
	    width = w;
	    height = h;
	    connectors = Genode::List<Connector> {};
	  }

	  unsigned count_connectors() {
	    unsigned count = 0;
	    for(Connector *item = connectors.first(); item; item = item->next()) {
	      count ++;
	    }
	    return count;
	  }
	};

        /**
	 * Signal handler that is invoked when the configuration or the ROM module
	 * changes.
	 */
	void _handle_update();

	Genode::Signal_handler<Main> _update_handler {
		_env.ep(), *this, &Main::_handle_update };

        Expanding_reporter _reporter { _env, "config", "fb_drv" };



        Main(Genode::Env &env) : _env(env)
	{
	        Genode::log("started");
		_config_rom.sigh(_update_handler);
		_handle_update();
	}
};



void Ada::Main::_handle_update()
{
        using namespace Genode;

	_config_rom.update();

	Genode::List<Mode> modeList {};
	unsigned number_of_connectors = 0;

        Genode::Xml_node config_node = _config_rom.xml();
	log("XML: ", config_node);
	try {
	  //Loop over the connector elements
	  config_node.for_each_sub_node([&] (Xml_node const &connector_node) {
	      log("connector node: ", connector_node);

	      Name connector_name;
	      connector_node.attribute("name").value(&connector_name);
	      //log("connector name: ", connector_name);

	      bool connected;
	      connector_node.attribute("connected").value(&connected);
	      //log("connector connected: ", connected);

	      if (connected) {
		number_of_connectors++;

		// Loop over the mode lines
		connector_node.for_each_sub_node([&] (Xml_node const &mode_node) {
		    log("mode node: ", mode_node);

		    unsigned width = 0;
		    unsigned height = 0;
		    unsigned hz = 0;
		    mode_node.attribute("width").value(&width);
		    mode_node.attribute("height").value(&height);
		    mode_node.attribute("hz").value(&hz);
		    //log("dimensions: ", width, height, hz);

		    //Connector &connector = new Connector { connector_name, hz };
		    Connector *connector = new(_heap) Connector(connector_name, hz);

		    // Find modeList item with same resolution
		    bool found = false;
		    for(Mode *item = modeList.first(); item; item = item->next()) {
		      log("item: ", item, " ", item->width, "x", item->height);
		      if (item->width == width && item->height == height) {
			// Add Connector to mode.
			log("item found");
			item->connectors.insert(connector);
			found = true;
			log("Modelist: ");//, modeList);
			break;
		      }
		    }
		    if (!found) {
		      log("item not found");
		      // Not in there, create a new modeline
		      Mode *item = new(_heap) Mode(width, height);
		      item->connectors.insert(connector);
		      //, List<Connector>{ connector } };
		      modeList.insert(item);
		    };
		  });
	      }
	    });
	} catch (Xml_node::Nonexistent_sub_node) {
	  log("XML PARSE ERROR");
	}

	log("Show what we have");
	for(Mode *item = modeList.first(); item; item = item->next()) {
	  log("Mode ", item->width, "x", item->height);
	  for(Connector *conn = item->connectors.first(); conn; conn = conn->next()) {
	    log("    Conn: ", conn->name, " ", conn->hz, "Hz");
	  }
	}

	log("Start filtering");

	// Filter out the modelines that have less than <number_of_connector> entries

	bool removed;// = false;
	do {
	  removed = false;
	  for(Mode *item = modeList.first(); item; item = item->next()) {
	    unsigned count = item->count_connectors();
	    log("modeline ", item->width, " has ", count);
	    if (count < number_of_connectors) {
	      log("remove it");
	      modeList.remove(item); // Side effect: Destroys iterator.
	      removed = true;
	    }
	  }
	} while (removed); // repeat until there are no more deletions

	log("check what's left");
	for(Mode *item = modeList.first(); item; item = item->next()) {
	  unsigned count = item->count_connectors();
	  log("modeline ", item->width, " has ", count);
	}

	// Who has the biggest (screen area)
	Mode* biggest = 0; // which mode
	unsigned max_area = 0; // has the biggest area so far
	for(Mode *item = modeList.first(); item; item = item->next()) {
	  unsigned area = item->width * item->height;
	  if (area > max_area) {
	    max_area = area;
	    biggest = item;
	  }
	}

	// Tell us
	if (max_area > 0) {
	  log("Biggest is: ", biggest->width, "x", biggest->height);

	  _reporter.generate([&] (Xml_generator  &xml) {
	      xml.attribute("width", biggest->width);
	      xml.attribute("height", biggest->height);
	      xml.attribute("buffered", "yes");

	      xml.node("report", [&] () {
		  xml.attribute("connectors", "yes");
		});

	      for(Connector *item = biggest->connectors.first(); item; item = item->next()) {
		xml.node("connector", [&] () {
		    xml.attribute("name", item->name);
		    xml.attribute("width", biggest->width);
		    xml.attribute("height", biggest->height);
		    xml.attribute("hz", item->hz);
		    xml.attribute("enabled", "yes");
		  });
	      };
	    });
	} else {
	  log("Only one screen connected");
	}
}

void Component::construct(Genode::Env &env) { static Ada::Main main(env); }
