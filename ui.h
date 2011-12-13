#ifndef MCMAP_UI_H
#define MCMAP_UI_H 1

void start_ui(bool map, int scale, bool resizable, int wnd_w, int wnd_h);
bool handle_scale_key(int *base_scale, int *scale, SDL_KeyboardEvent *e);
void handle_chat(struct buffer msg);

#endif /* MCMAP_UI_H */
