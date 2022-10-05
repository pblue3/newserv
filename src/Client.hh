#pragma once

#include <netinet/in.h>

#include <memory>

#include "Channel.hh"
#include "CommandFormats.hh"
#include "FileContentsCache.hh"
#include "FunctionCompiler.hh"
#include "License.hh"
#include "PatchFileIndex.hh"
#include "Player.hh"
#include "PSOEncryption.hh"
#include "PSOProtocol.hh"
#include "Text.hh"



extern const uint64_t CLIENT_CONFIG_MAGIC;
extern FileContentsCache client_options_cache;



struct Client {
  enum Flag {
    // This flag has two meanings. If set on a client with GameVersion::DC, then
    // IS_DC_V1 is also set. In this case, the client is DC Network Trial
    // Edition, which uses several commands that no other version uses. If this
    // flag is set without IS_DC_V1, then the client is GC Episodes 1 & 2 Trial
    // Edition, and therefore uses V2 encryption instead of V3 encryption, and
    // doesn't support some commands.
    // Note that this flag is NOT set for Episode 3 Trial Edition clients, since
    // that version is similar enough to the release version of Episode 3 that
    // newserv does not have to change its behavior at all.
    IS_TRIAL_EDITION                 = 0x2000,
    // Client is DC v1
    IS_DC_V1                         = 0x0010,
    // For patch server clients, client is Blue Burst rather than PC
    IS_BB_PATCH                      = 0x0001,
    // After joining a lobby, client will no longer send D6 commands when they
    // close message boxes
    NO_D6_AFTER_LOBBY                = 0x0002,
    // Client has the above flag and has already joined a lobby, or is not GC
    NO_D6                            = 0x0004,
    // Client is Episode 3, should be able to see CARD lobbies, and should only
    // be able to see/join games with the EPISODE_3_ONLY flag
    IS_EPISODE_3                     = 0x0008,
    // Client disconnects if it receives B2 (send_function_call)
    NO_SEND_FUNCTION_CALL            = 0x0200,
    // Client requires doubly-encrypted code section in send_function_call
    ENCRYPTED_SEND_FUNCTION_CALL     = 0x0800,
    // Client supports send_function_call but does not actually run the code
    SEND_FUNCTION_CALL_CHECKSUM_ONLY = 0x1000,

    // Client is loading into a game
    LOADING                          = 0x0020,
    // Client is loading a quest
    LOADING_QUEST                    = 0x0040,
    // Client is in the information menu (login server only)
    IN_INFORMATION_MENU              = 0x0080,
    // Client is at the welcome message (login server only)
    AT_WELCOME_MESSAGE               = 0x0100,
    // Client has already received a 97 (enable saves) command, so don't show
    // the programs menu anymore
    SAVE_ENABLED                     = 0x0400,
    // Client has received newserv's Episode 3 card definitions, so don't send
    // them again
    HAS_EP3_CARD_DEFS                = 0x4000,
  };

  uint64_t id;
  PrefixedLogger log;

  // License & account
  std::shared_ptr<const License> license;

  // Note: these fields are included in the client config. On GC, the client
  // config can be up to 0x20 bytes; on BB it can be 0x28 bytes. We don't use
  // all of that space.
  uint8_t bb_game_state;
  uint16_t flags;

  // Network
  Channel channel;
  struct sockaddr_storage next_connection_addr;
  ServerBehavior server_behavior;
  bool should_disconnect;
  bool should_send_to_lobby_server;
  bool should_send_to_proxy_server;
  uint32_t proxy_destination_address;
  uint16_t proxy_destination_port;

  // Patch server
  std::vector<PatchFileChecksumRequest> patch_file_checksum_requests;

  // Lobby/positioning
  float x;
  float z;
  uint32_t area; // which area is the client in?
  uint32_t lobby_id; // which lobby is this person in?
  uint8_t lobby_client_id; // which client number is this person?
  uint8_t lobby_arrow_color; // lobby arrow color ID
  bool prefer_high_lobby_client_id;
  int64_t preferred_lobby_id; // <0 = no preference
  ClientGameData game_data;
  std::unique_ptr<struct event, void(*)(struct event*)> save_game_data_event;
  int16_t card_battle_table_number;
  uint8_t card_battle_table_seat_number;

  // Miscellaneous (used by chat commands)
  uint32_t next_exp_value; // next EXP value to give
  int16_t override_section_id; // valid if >= 0
  int64_t override_random_seed; // valid if >= 0
  bool infinite_hp; // cheats enabled
  bool infinite_tp; // cheats enabled
  bool switch_assist; // cheats enabled
  G_SwitchStateChanged_6x05 last_switch_enabled_command;
  bool can_chat;
  std::string pending_bb_save_username;
  uint8_t pending_bb_save_player_index;

  bool proxy_save_files;
  bool proxy_suppress_remote_login;
  bool proxy_zero_remote_guild_card;

  // DOL file loading state
  uint32_t dol_base_addr;
  std::shared_ptr<DOLFileIndex::DOLFile> loading_dol_file;

  Client(struct bufferevent* bev, GameVersion version, ServerBehavior server_behavior);

  inline GameVersion version() const {
    return this->channel.version;
  }

  void set_license(std::shared_ptr<const License> l);

  ClientConfig export_config() const;
  ClientConfigBB export_config_bb() const;
  void import_config(const ClientConfig& cc);
  void import_config(const ClientConfigBB& cc);

  static void dispatch_save_game_data(evutil_socket_t, short, void* ctx);
  void save_game_data();
};
