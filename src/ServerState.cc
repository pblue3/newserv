#include "ServerState.hh"

#include <string.h>

#include <memory>

#include "SendCommands.hh"
#include "NetworkAddresses.hh"
#include "IPStackSimulator.hh"
#include "Text.hh"

using namespace std;



ServerState::ServerState()
  : dns_server_port(0),
    ip_stack_debug(false),
    allow_unregistered_users(false),
    run_shell_behavior(RunShellBehavior::DEFAULT), next_lobby_id(1),
    pre_lobby_event(0),
    ep3_menu_song(-1) {
  memset(&this->default_key_file, 0, sizeof(this->default_key_file));

  vector<shared_ptr<Lobby>> ep3_only_lobbies;

  for (size_t x = 0; x < 20; x++) {
    auto lobby_name = decode_sjis(string_printf("LOBBY%zu", x + 1));
    bool is_ep3_only = (x > 14);

    shared_ptr<Lobby> l(new Lobby());
    l->flags |= Lobby::Flag::PUBLIC | Lobby::Flag::DEFAULT | Lobby::Flag::PERSISTENT |
        (is_ep3_only ? Lobby::Flag::EPISODE_3_ONLY : 0);
    l->block = x + 1;
    l->type = x;
    l->name = lobby_name;
    l->max_clients = 12;
    this->add_lobby(l);

    if (!is_ep3_only) {
      this->public_lobby_search_order.emplace_back(l);
    } else {
      ep3_only_lobbies.emplace_back(l);
    }
  }

  this->public_lobby_search_order_ep3 = this->public_lobby_search_order;
  this->public_lobby_search_order_ep3.insert(
      this->public_lobby_search_order_ep3.begin(),
      ep3_only_lobbies.begin(),
      ep3_only_lobbies.end());
}

void ServerState::add_client_to_available_lobby(shared_ptr<Client> c) {
  const auto& search_order = (c->flags & Client::Flag::EPISODE_3)
      ? this->public_lobby_search_order_ep3
      : this->public_lobby_search_order;

  shared_ptr<Lobby> added_to_lobby;
  for (const auto& l : search_order) {
    try {
      l->add_client(c);
      added_to_lobby = l;
      break;
    } catch (const out_of_range&) { }
  }

  if (!added_to_lobby) {
    // TODO: Add the user to a dynamically-created private lobby instead
    throw out_of_range("all lobbies full");
  }

  // Send a join message to the joining player, and notifications to all others
  this->send_lobby_join_notifications(added_to_lobby, c);
}

void ServerState::remove_client_from_lobby(shared_ptr<Client> c) {
  auto l = this->id_to_lobby.at(c->lobby_id);
  l->remove_client(c);
  if (!(l->flags & Lobby::Flag::PERSISTENT) && (l->count_clients() == 0)) {
    this->remove_lobby(l->lobby_id);
  } else {
    send_player_leave_notification(l, c->lobby_client_id);
  }
}

void ServerState::change_client_lobby(shared_ptr<Client> c, shared_ptr<Lobby> new_lobby) {
  uint8_t old_lobby_client_id = c->lobby_client_id;

  shared_ptr<Lobby> current_lobby = this->find_lobby(c->lobby_id);
  try {
    if (current_lobby) {
      current_lobby->move_client_to_lobby(new_lobby, c);
    } else {
      new_lobby->add_client(c);
    }
  } catch (const out_of_range&) {
    send_lobby_message_box(c, u"$C6Can't change lobby\n\n$C7The lobby is full.");
    return;
  }

  if (current_lobby) {
    if (!(current_lobby->flags & Lobby::Flag::PERSISTENT) && (current_lobby->count_clients() == 0)) {
      this->remove_lobby(current_lobby->lobby_id);
    } else {
      send_player_leave_notification(current_lobby, old_lobby_client_id);
    }
  }
  this->send_lobby_join_notifications(new_lobby, c);
}

void ServerState::send_lobby_join_notifications(shared_ptr<Lobby> l,
    shared_ptr<Client> joining_client) {
  for (auto& other_client : l->clients) {
    if (!other_client) {
      continue;
    } else if (other_client == joining_client) {
      send_join_lobby(joining_client, l);
    } else {
      send_player_join_notification(other_client, l, joining_client);
    }
  }
}

shared_ptr<Lobby> ServerState::find_lobby(uint32_t lobby_id) {
  return this->id_to_lobby.at(lobby_id);
}

vector<shared_ptr<Lobby>> ServerState::all_lobbies() {
  vector<shared_ptr<Lobby>> ret;
  for (auto& it : this->id_to_lobby) {
    ret.emplace_back(it.second);
  }
  return ret;
}

void ServerState::add_lobby(shared_ptr<Lobby> l) {
  l->lobby_id = this->next_lobby_id++;
  if (this->id_to_lobby.count(l->lobby_id)) {
    throw logic_error("lobby already exists with the given id");
  }
  this->id_to_lobby.emplace(l->lobby_id, l);
}

void ServerState::remove_lobby(uint32_t lobby_id) {
  this->id_to_lobby.erase(lobby_id);
}

shared_ptr<Client> ServerState::find_client(const char16_t* identifier,
    uint64_t serial_number, shared_ptr<Lobby> l) {

  if ((serial_number == 0) && identifier) {
    try {
      string encoded = encode_sjis(identifier);
      serial_number = stoull(encoded, nullptr, 0);
    } catch (const exception&) { }
  }

  // look in the current lobby first
  if (l) {
    try {
      return l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  // look in all lobbies if not found
  for (auto& other_l : this->all_lobbies()) {
    if (l == other_l) {
      continue; // don't bother looking again
    }
    try {
      return other_l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  throw out_of_range("client not found");
}

uint32_t ServerState::connect_address_for_client(std::shared_ptr<Client> c) {
  if (c->is_virtual_connection) {
    if (c->remote_addr.ss_family != AF_INET) {
      throw logic_error("virtual connection is missing remote IPv4 address");
    }
    const auto* sin = reinterpret_cast<const sockaddr_in*>(&c->remote_addr);
    return IPStackSimulator::connect_address_for_remote_address(
        ntohl(sin->sin_addr.s_addr));
  } else {
    // TODO: we can do something smarter here, like use the sockname to find
    // out which interface the client is connected to, and return that address
    if (is_local_address(c->remote_addr)) {
      return this->local_address;
    } else {
      return this->external_address;
    }
  }
}



const vector<MenuItem>& ServerState::proxy_destinations_menu_for_version(GameVersion version) {
  if (version == GameVersion::PC) {
    return this->proxy_destinations_menu_pc;
  } else if (version == GameVersion::GC) {
    return this->proxy_destinations_menu_gc;
  }
  throw out_of_range("no proxy destinations menu exists for this version");
}

const vector<pair<string, uint16_t>>& ServerState::proxy_destinations_for_version(GameVersion version) {
  if (version == GameVersion::PC) {
    return this->proxy_destinations_pc;
  } else if (version == GameVersion::GC) {
    return this->proxy_destinations_gc;
  }
  throw out_of_range("no proxy destinations menu exists for this version");
}



void ServerState::set_port_configuration(
    const vector<PortConfiguration>& port_configs) {
  this->name_to_port_config.clear();
  this->number_to_port_config.clear();
  for (const auto& pc : port_configs) {
    shared_ptr<PortConfiguration> spc(new PortConfiguration(pc));
    if (!this->name_to_port_config.emplace(spc->name, spc).second) {
      throw logic_error("duplicate name in port configuration");
    }
    if (!this->number_to_port_config.emplace(spc->port, spc).second) {
      throw logic_error("duplicate number in port configuration");
    }
  }
}
