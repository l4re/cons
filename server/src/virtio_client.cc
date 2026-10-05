#include "virtio_client.h"

void
Virtio_cons::handle_tx()
{
  using namespace L4virtio::Svr;
  Virtqueue &q = _q[Tx];

  Request_processor p;
  Virtqueue::Head_desc h;
  Buffer b;

  while (auto r = q.next_avail())
    {
      h = p.start(mem_info(), r, &b);
      for (;;)
        {
          cooked_write(b.pos, b.left);
          if (!p.next(mem_info(), &b))
            break;
        }
      q.finish(h, this);
    }
}

void
Virtio_cons::handle_rx()
{
  char const *d = 0;
  unsigned rs = rbuf()->get(0, &d);
  if (!rs)
    return; // no input

  using namespace L4virtio::Svr;
  Virtqueue &q = _q[Rx];

  Request_processor p;
  Virtqueue::Head_desc h;
  Buffer b;

  Data_buffer src;
  src.pos = const_cast<char *>(d);
  src.left = rs;

  for (;;)
    {
      auto r = q.next_avail();
      if (!r)
        {
          // No receive buffer posted by the driver. Drop what was already
          // handed over and keep the rest queued for the next kick, otherwise
          // it would be delivered a second time.
          rbuf()->clear(rs - src.left);
          return;
        }

      l4_uint32_t total = 0;
      h = p.start(mem_info(), r, &b);
      for (;;)
        {
          total += src.copy_to(&b);
          if (!src.left)
            {
              rbuf()->clear(rs);
              rs = rbuf()->get(0, &d);
              if (!rs)
                {
                  q.finish(h, this, total);
                  return;
                }

              src.pos = const_cast<char *>(d);
              src.left = rs;
              continue;
            }
          else if (!p.next(mem_info(), &b))
            {
              q.finish(h, this, total);
              break;
            }
        }
    }
}

void
Virtio_cons::kick()
{
  try
    {
      if (L4_LIKELY(_q[Tx].ready()))
        handle_tx();

      if (L4_LIKELY(_q[Rx].ready()))
        handle_rx();
    }
  catch (L4virtio::Svr::Bad_descriptor const &e)
    {
      printf("cons: bad virtio descriptor from '%s': %s\n",
             tag().c_str(), e.message());
      device_error();
    }
}
