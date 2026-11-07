#ifndef PS3_CONFIG_H
#define PS3_CONFIG_H

enum {
	PS3_ACT_I = 0,
	PS3_ACT_II,
	PS3_ACT_RUN,
	PS3_ACT_SELECT,
	PS3_ACT_SAVE,
	PS3_ACT_LOAD,
	PS3_ACT_COUNT
};

struct ps3_config {
	int volume;
	int slot;
	int arcade_card;
	int tv_size;
	int filter;
	unsigned int key[PS3_ACT_COUNT];
};

extern struct ps3_config ps3_cfg;

void ps3_config_defaults(void);
void ps3_config_load(void);
void ps3_config_save(void);
const char *ps3_config_button_name(unsigned int mask);

#endif
