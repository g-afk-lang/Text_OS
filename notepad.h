#ifndef NOTEPAD_H
#define NOTEPAD_H

// --- NOTEPAD FUNCTION DECLARATIONS ---
bool is_notepad_running();
void notepad_handle_input(char key);
void notepad_handle_special_key(int scancode);
void start_notepad(const char* filename);
void cmd_notepad(const char* filename);

// --- INTERNAL NOTEPAD FUNCTIONS ---
void notepad_clear_buffer();
void notepad_draw_interface();
void notepad_update_cursor();
void notepad_insert_char(char c);
void notepad_delete_char();
void notepad_new_line();
void notepad_move_cursor(int delta_row, int delta_col);
void notepad_save_and_exit();
void notepad_load_file(const char* filename);



#endif // NOTEPAD_H
