#ifndef TEST_H
#define TEST_H

/**
 * @brief Initializes and starts the Pong game.
 * Sets the game state to active, resets scores, and positions the ball and paddles.
 */
void start_pong_game();

/**
 * @brief Updates the game state.
 * This function should be called repeatedly by a timer interrupt (e.g., in your timer_handler).
 * It moves the ball, handles collisions, updates the AI paddle, and redraws the screen.
 */
void pong_update();

/**
 * @brief Handles keyboard input for the player.
 * This function should be called from your keyboard_handler when a key is pressed.
 * @param key The ASCII character of the key that was pressed.
 */
void pong_handle_input(char key);

/**
 * @brief Checks if the game is currently running.
 * @return true if the game is active, false otherwise.
 */
bool is_pong_running();

#endif // TEST_H

