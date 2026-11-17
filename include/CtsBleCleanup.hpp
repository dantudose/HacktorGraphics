#pragma once

namespace watch {

// CTS owns only its client when sharing the activity peripheral's host. A
// successful deleteClient may defer deletion until the disconnect callback;
// never inspect the client again after handing it back to the device.
template <typename Device, typename Client>
bool releaseCtsBle(Client* client, bool shared_radio) {
  const bool client_released = !client || Device::deleteClient(client);
  if (shared_radio) return client_released;

  // A private-client release failure must not prevent an exclusive session
  // from stopping its host. Preserve all callback objects if that stop fails.
  if (!Device::deinit(false)) return false;
  const bool objects_released = Device::deinit(true);
  return client_released && objects_released;
}

}  // namespace watch
