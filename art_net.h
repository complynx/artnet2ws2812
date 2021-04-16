#ifndef ART_NET_H_
#define ART_NET_H_

#ifndef ART_NET_PORT
#define ART_NET_PORT 6454
#endif

#ifndef ART_NET_SHIFT
#define ART_NET_SHIFT 0
#endif
#ifndef ART_NET_UNIVERSE
#define ART_NET_UNIVERSE 0
#endif

#ifndef SEQUENCE_ROLLOVER_TOLERANCE
#define SEQUENCE_ROLLOVER_TOLERANCE 30
#endif

void init_server();

#endif /* ART_NET_H_ */
