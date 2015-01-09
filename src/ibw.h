#ifndef IBW_H
#define IBW_H

int ibwloadcfg(const char *cfgfile);
int ibwadd(const char *iface, uint32_t limit);
void ibwlist(void);
int ibwget(const char *iface);
ibwnode *ibwgetnode(const char *iface);
void ibwflush(void);
int ibwcfgread(FILE *fd);

#endif
