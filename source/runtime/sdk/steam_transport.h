#ifndef MOON_STEAM_TRANSPORT_H
#define MOON_STEAM_TRANSPORT_H
/* Native IL2CPP boundary, included after the owning gameplay/unity headers.
 * Main-thread serialized calls only. Each adapter supplies its immutable channel.
 * Membership is Steam/session authority; the payload never selects its sender.
 */
/* Protocols own their channels, payloads, retry rules and saved identity. This
 * adapter owns the managed boundary, membership checks and handle release. */
#ifndef MOON_TRANSPORT_CHANNEL
#error "Define the protocol's reserved MOON_TRANSPORT_CHANNEL before including"
#endif
#define ST_CHANNEL MOON_TRANSPORT_CHANNEL
#define ST_BATCH 16
#define ST_MAX_BYTES 1056
_Static_assert(ST_CHANNEL > 2 && ST_CHANNEL <= 0x7fffffff, "reserved Steam channel");

typedef void (*STReceive)(U64 authenticated_peer, const unsigned char *bytes,
                          unsigned int length, void *ctx);
static struct {
  PTR instance, send, receive, from_pointer, release, identity_id;
  PTR local_id, host_id, joined, member, remote_id, shown;
  PTR data_field, size_field, identity_field, channel_field;
  int identity_offset;
  SURoot incoming;
  int ready, busy;
  U64 messages[ST_BATCH];
  unsigned char payload[ST_MAX_BYTES];
  unsigned int delivered, dropped, sent, send_failed, release_failed;
} st;

static inline void st_count(unsigned int *counter) {
  if (*counter != 0xffffffffu)
    (*counter)++;
}
static inline int st_bool(PTR method_id, void **args) {
  int ok = 1;
  PTR result = call(method_id, 0, args, &ok);
  return ok && result && *(unsigned char *)unbox(result) != 0;
}
static inline U64 st_u64(PTR method_id, PTR self) {
  U64 id = 0;
  int ok = 1;
  PTR result = call(method_id, self, 0, &ok);
  if (ok && result)
    su_copy(&id, unbox(result), sizeof(id));
  return id;
}
static inline int st_joined(void) { return st.ready && st_bool(st.joined, 0); }
static inline U64 st_local_id(void) {
  return st.ready ? st_u64(st.local_id, 0) : 0;
}
static inline U64 st_host_id(void) {
  return st_joined() ? st_u64(st.host_id, 0) : 0;
}
static inline int st_member(U64 peer) {
  void *args[] = {&peer};
  return peer && st_bool(st.member, args);
}
static inline void st_close(void) {
  if (st.busy)
    return;
  su_drop(&st.incoming);
  su_zero(&st, sizeof(st));
}
static inline int st_bind(PTR game) {
  (void)game;
  if (st.ready)
    return 1;
  if (st.busy || !su.ready || !su.array_address || !su.array_length ||
      !su.static_field || !su.intptr_class || !su.byte_class)
    return 0;
#define ST_METHOD(field, ns, type, name, signature)                            \
  st.field = su_method(ns, type, name, signature);                             \
  if (!st.field)                                                               \
  return 0
  ST_METHOD(instance, "MoonCleaners", "MoonCrewSession", "get_Instance", "");
  ST_METHOD(
      send, "MoonCleaners", "MoonCrewSession", "SendBytes",
      "System.UInt64;System.Byte[];System.Int32;System.Int32;System.Boolean");
  ST_METHOD(member, "MoonCleaners", "MoonCrewSession", "IsMember",
            "System.UInt64");
  ST_METHOD(receive, "Steamworks", "SteamNetworkingMessages",
            "ReceiveMessagesOnChannel",
            "System.Int32;System.IntPtr[];System.Int32");
  ST_METHOD(from_pointer, "Steamworks", "SteamNetworkingMessage_t",
            "FromIntPtr", "System.IntPtr");
  ST_METHOD(release, "Steamworks", "SteamNetworkingMessage_t", "Release",
            "System.IntPtr");
  ST_METHOD(identity_id, "Steamworks", "SteamNetworkingIdentity", "GetSteamID",
            "");
  ST_METHOD(local_id, "Steamworks", "SteamUser", "GetSteamID", "");
  ST_METHOD(host_id, "MoonCleaners", "MoonCrew", "get_HostId", "");
  ST_METHOD(joined, "MoonCleaners", "MoonCrew", "get_Joined", "");
  ST_METHOD(remote_id, "MoonCleaners", "MoonRemotePlayer", "get_SteamId", "");
#undef ST_METHOD
  PTR message = find_class("Steamworks", "SteamNetworkingMessage_t");
  PTR remote = find_class("MoonCleaners", "MoonRemotePlayer");
  PTR steam_id = find_class("Steamworks", "CSteamID");
  if (!message || !remote || !steam_id)
    return 0;
  st.data_field = get_field(message, "m_pData");
  st.size_field = get_field(message, "m_cbSize");
  st.identity_field = get_field(message, "m_identityPeer");
  st.channel_field = get_field(message, "m_nChannel");
  st.shown = get_field(remote, "Shown");
  PTR raw_id = get_field(steam_id, "m_SteamID");
  if (!st.data_field || !st.size_field || !st.identity_field ||
      !st.channel_field || !st.shown || !raw_id ||
      su.field_offset(st.data_field) != 16 ||
      su.field_offset(st.size_field) != 24 ||
      su.field_offset(st.identity_field) != 32 ||
      su.field_offset(st.channel_field) != 88 || su.field_offset(raw_id) != 16)
    return 0;
  st.identity_offset = su.field_offset(st.identity_field) - 16;
  PTR incoming = su.new_array(su.intptr_class, ST_BATCH);
  st.incoming = su_hold(incoming);
  if (!st.incoming)
    return 0;
  st.ready = 1;
  return 1;
}

/* A result means the native send accepted the request, not that a peer has
 * acknowledged or applied it. The caller owns protocol/retry/sequence rules. */
static inline int st_send_mode(U64 peer, const unsigned char *bytes,
                          unsigned int length,unsigned char reliable) {
  U64 local = st_local_id();
  if (!st.ready || !bytes || !length || length > ST_MAX_BYTES || !st_joined() ||
      !local || peer == local || !st_member(peer)) {
    st_count(&st.send_failed);
    return 0;
  }
  int ok = 1, count = (int)length, channel = ST_CHANNEL;
  PTR session = call(st.instance, 0, 0, &ok);
  SURoot session_root = su_hold(ok ? session : 0);
  if (!session_root) {
    st_count(&st.send_failed);
    return 0;
  }
  PTR array = su.new_array(su.byte_class, length);
  SURoot array_root = su_hold(array);
  int accepted = 0;
  if (array_root && su.array_length(su_get(array_root)) == length) {
    PTR data = su.array_address(su_get(array_root), 1, 0);
    if (data) {
      su_copy(data, bytes, length);
      void *args[] = {&peer, su_get(array_root), &count, &channel, &reliable};
      PTR result = call(st.send, su_get(session_root), args, &ok);
      accepted = ok && result && *(unsigned char *)unbox(result);
    }
  }
  su_drop(&array_root);
  su_drop(&session_root);
  st_count(accepted ? &st.sent : &st.send_failed);
  return accepted;
}
static inline int st_send(U64 peer,const unsigned char *bytes,unsigned length){return st_send_mode(peer,bytes,length,1);}

static inline void st_release(U64 handle) {
  if (!handle)
    return;
  int ok = 1;
  void *args[] = {&handle};
  call(st.release, 0, args, &ok);
  if (!ok)
    st_count(&st.release_failed);
}

/* At most sixteen messages per invocation; the caller may schedule another
 * bounded drain next frame. Release occurs before the user callback, whose
 * copied payload is valid only for that callback. Reentrant receive is refused.
 */
static inline unsigned int st_receive(STReceive callback, void *ctx) {
  if (!st.ready || st.busy || !st_joined())
    return 0;
  st.busy = 1;
  PTR array = su_get(st.incoming);
  unsigned int delivered = 0;
  int count = -1, ok = 1, channel = ST_CHANNEL, maximum = ST_BATCH;
  PTR slots = array && su.array_length(array) == ST_BATCH
                  ? su.array_address(array, (int)sizeof(U64), 0)
                  : 0;
  if (!slots) {
    st.busy = 0;
    return 0;
  }
  su_zero(slots, ST_BATCH * sizeof(U64));
  void *args[] = {&channel, array, &maximum};
  PTR result = call(st.receive, 0, args, &ok);
  if (ok && result)
    su_copy(&count, unbox(result), sizeof(count));
  /* Copy returned pointers before any further managed call, and clear slots
   * even on an exception/invalid count. Every obtained unique handle releases.
   */
  array = su_get(st.incoming);
  slots = array ? su.array_address(array, (int)sizeof(U64), 0) : 0;
  if (!slots) {
    st.busy = 0;
    return 0;
  }
  su_copy(st.messages, slots, ST_BATCH * sizeof(U64));
  su_zero(slots, ST_BATCH * sizeof(U64));
  U64 local_id = st_local_id();
  int valid_batch = ok && count >= 0 && count <= ST_BATCH;
  for (int i = 0; i < ST_BATCH; i++) {
    U64 handle = st.messages[i], peer = 0;
    int duplicate = 0, valid = valid_batch && i < count, length = 0,
        message_channel = -1;
    PTR data = 0;
    SURoot message_root = 0;
    if (!handle)
      continue;
    for (int j = 0; j < i; j++)
      if (st.messages[j] == handle)
        duplicate = 1;
    if (duplicate) {
      st_count(&st.dropped);
      continue;
    }
    if (valid) {
      ok = 1;
      void *from_args[] = {&handle};
      PTR boxed = call(st.from_pointer, 0, from_args, &ok);
      message_root = su_hold(ok ? boxed : 0);
      valid = message_root != 0;
      if (valid) {
        boxed = su_get(message_root);
        field_value(boxed, st.data_field, &data);
        field_value(boxed, st.size_field, &length);
        field_value(boxed, st.channel_field, &message_channel);
        valid = data && length > 0 && length <= ST_MAX_BYTES &&
                message_channel == ST_CHANNEL;
        if (valid) {
          PTR identity =
              (unsigned char *)unbox(su_get(message_root)) + st.identity_offset;
          peer = st_u64(st.identity_id, identity);
          valid = local_id && peer && peer != local_id && st_member(peer);
          if (valid)
            su_copy(st.payload, data, (U64)length);
        }
      }
    }
    su_drop(&message_root);
    st_release(handle);
    if (valid && callback) {
      callback(peer, st.payload, (unsigned int)length, ctx);
      delivered++;
      st_count(&st.delivered);
    } else {
      st_count(&st.dropped);
    }
  }
  st.busy = 0;
  return delivered;
}

/* Shown supplies current loaded remote identities; IsMember is still the
 * authority. Return unique remote peers only, never the local player. */
static inline unsigned int st_peers(U64 *out, unsigned int capacity) {
  if (!out || !capacity || !st_joined())
    return 0;
  if (capacity > ST_BATCH)
    capacity = ST_BATCH;
  PTR list = 0;
  su.static_field(st.shown, &list);
  SURoot list_root = su_hold(list);
  if (!list_root)
    return 0;
  PTR klass = object_class(su_get(list_root));
  PTR count_method = get_method(klass, "get_Count", 0);
  PTR item_method = get_method(klass, "get_Item", 1);
  int ok = 1, count = integer(count_method, su_get(list_root), &ok);
  unsigned int written = 0;
  U64 local_id = st_local_id();
  if (ok && count >= 0 && count <= ST_BATCH && local_id && item_method) {
    for (int i = 0; i < count && written < capacity; i++) {
      void *args[] = {&i};
      ok = 1;
      PTR remote = call(item_method, su_get(list_root), args, &ok);
      SURoot remote_root = su_hold(ok ? remote : 0);
      U64 id = remote_root ? st_u64(st.remote_id, su_get(remote_root)) : 0;
      su_drop(&remote_root);
      if (!id || id == local_id || !st_member(id))
        continue;
      unsigned int j;
      for (j = 0; j < written; j++)
        if (out[j] == id)
          break;
      if (j == written)
        out[written++] = id;
    }
  }
  su_drop(&list_root);
  return written;
}
#endif
