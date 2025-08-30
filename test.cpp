#include "test.h"
#include "terminal_hooks.h"

// --- Game Constants ---
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define PADDLE_HEIGHT 5
#define PADDLE_CHAR '#'
#define BALL_CHAR 'O'

// --- AI Constants ---
#define AI_MISS_CHANCE 64        // 1 in 8 chance to miss per update
#define AI_REACTION_DELAY 1     // AI reacts slower than perfect
#define AI_SPEED_REDUCTION 0.3f // AI moves slower than it could

// --- Game Object Structure ---
struct GameObject {
    float x, y;
    float vx, vy; // Velocity
};

// --- Game State Variables ---
static GameObject player1;
static GameObject player2;
static GameObject ball;
static int score1 = 0;
static int score2 = 0;
static bool game_is_active = false;
static int frame_counter = 0;

// --- AI State Variables ---
static int ai_reaction_counter = 0;
static bool ai_is_missing = false;
static int ai_miss_timer = 0;
static uint32_t random_seed = 12345; // Simple PRNG seed

// --- Random Number Generator ---
static uint32_t simple_rand() {
    random_seed = random_seed * 1103515245 + 12345;
    return (random_seed / 65536) % 32768;
}

// --- Private Function Declarations ---
static void reset_ball();
static void pong_render();
static void draw_char_at(char c, int x, int y);
static void draw_string_at(const char* str, int x, int y);
static void clear_game_screen();

/**
 * @brief Draws a character at specific coordinates using your terminal system.
 */
void draw_char_at(char c, int x, int y) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        terminal_putentryat(c, terminal_color, x, y);
    }
}

/**
 * @brief Draws a string at specific coordinates.
 */
void draw_string_at(const char* str, int x, int y) {
    int i = 0;
    while (str[i] != '\0' && (x + i) < VGA_WIDTH) {
        draw_char_at(str[i], x + i, y);
        i++;
    }
}

/**
 * @brief Clears the game screen area.
 */
void clear_game_screen() {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            draw_char_at(' ', x, y);
        }
    }
}

/**
 * @brief Resets the ball to the center with a random velocity.
 */
void reset_ball() {
    ball.x = SCREEN_WIDTH / 2.0f;
    ball.y = SCREEN_HEIGHT / 2.0f;
    
    // Set initial velocity (alternating direction each reset)
    static bool ball_direction = true;
    ball.vx = ball_direction ? 0.5f : -0.5f;
    ball.vy = 0.25f;
    ball_direction = !ball_direction;
    
    // Reset AI state when ball resets
    ai_is_missing = false;
    ai_miss_timer = 0;
    ai_reaction_counter = 0;
}

/**
 * @brief Simple integer to string conversion.
 */
void int_to_string(int num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    int i = 0;
    bool negative = false;
    
    if (num < 0) {
        negative = true;
        num = -num;
    }
    
    // Convert digits in reverse
    while (num > 0) {
        str[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    if (negative) {
        str[i++] = '-';
    }
    
    str[i] = '\0';
    
    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

/**
 * @brief Initializes and starts the game.
 */
void start_pong_game() {
    score1 = 0;
    score2 = 0;
    game_is_active = true;
    frame_counter = 0;

    // Initialize player paddles
    player1.x = 2;
    player1.y = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;
    player2.x = SCREEN_WIDTH - 3;
    player2.y = (SCREEN_HEIGHT - PADDLE_HEIGHT) / 2;

    // Reset AI state
    ai_is_missing = false;
    ai_miss_timer = 0;
    ai_reaction_counter = 0;

    reset_ball();
    
    // Clear screen and render initial state
    clear_game_screen();
    pong_render();
}

/**
 * @brief Renders the entire game scene.
 */
void pong_render() {
    // Clear previous frame
    clear_game_screen();
    
    // Draw top and bottom borders
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        draw_char_at('-', x, 0);
        draw_char_at('-', x, SCREEN_HEIGHT - 1);
    }
    
    // Draw center line
    for (int y = 1; y < SCREEN_HEIGHT - 1; y++) {
        if (y % 2 == 0) {
            draw_char_at('|', SCREEN_WIDTH / 2, y);
        }
    }

    // Draw paddles
    for (int i = 0; i < PADDLE_HEIGHT; ++i) {
        draw_char_at(PADDLE_CHAR, (int)player1.x, (int)player1.y + i);
        draw_char_at(PADDLE_CHAR, (int)player2.x, (int)player2.y + i);
    }

    // Draw ball
    draw_char_at(BALL_CHAR, (int)ball.x, (int)ball.y);

    // Draw scores
    char score_str[10];
    int_to_string(score1, score_str);
    draw_string_at(score_str, SCREEN_WIDTH / 2 - 10, 2);
    
    int_to_string(score2, score_str);
    draw_string_at(score_str, SCREEN_WIDTH / 2 + 8, 2);
    
    // Draw instructions and AI status
    draw_string_at("Player 1: W/S to move  ESC to quit", 5, SCREEN_HEIGHT - 3);
    
    // Show AI state for debugging (optional)
    if (ai_is_missing) {
        //draw_string_at("AI MISSING!", SCREEN_WIDTH - 15, 3);
    }
}

/**
 * @brief Handles player input for paddle movement.
 */
void pong_handle_input(char key) {
    if (!game_is_active) return;

    switch (key) {
        case 'w':
        case 'W':
            // Move player 1 up
            if (player1.y > 1) {
                player1.y -= 1.0f;
            }
            break;
        case 's':
        case 'S':
            // Move player 1 down
            if (player1.y < SCREEN_HEIGHT - PADDLE_HEIGHT - 1) {
                player1.y += 1.0f;
            }
            break;
        case 27: // ESC key
            game_is_active = false;
            clear_screen(); // Return to normal terminal
            // Restore cursor
            enable_hardware_cursor(14, 15);
            update_hardware_cursor(0, 0);
            break;
        case 'q':
        case 'Q':
            // Alternative quit key
            game_is_active = false;
            clear_screen();
            enable_hardware_cursor(14, 15);
            update_hardware_cursor(0, 0);
            break;
    }
}

/**
 * @brief Updates AI paddle with imperfect behavior.
 */
/**
 * @brief Updates AI paddle with imperfect behavior.
 */
void update_ai_paddle() {
    // Increment reaction counter
    ai_reaction_counter++;
    
    // Check if AI should start missing the ball
    if (!ai_is_missing && simple_rand() % AI_MISS_CHANCE == 0) {
        ai_is_missing = true;
        ai_miss_timer = 30 + (simple_rand() % 30); // Miss for 30-60 updates
    }
    
    // Handle missing behavior
    if (ai_is_missing) {
        ai_miss_timer--;
        if (ai_miss_timer <= 0) {
            ai_is_missing = false;
        } else {
            // AI moves in wrong direction or doesn't move at all
            int miss_behavior = simple_rand() % 3;
            switch (miss_behavior) {
                case 0: // Move in wrong direction
                    if (player2.y + PADDLE_HEIGHT / 2.0f > ball.y && player2.y > 1) {
                        player2.y -= 0.2f;
                    }
                    if (player2.y + PADDLE_HEIGHT / 2.0f < ball.y && player2.y < SCREEN_HEIGHT - PADDLE_HEIGHT - 1) {
                        player2.y += 0.2f;
                    }
                    break;
                case 1: // Don't move at all
                    break;
                case 2: // Move very slowly in correct direction
                    if (player2.y + PADDLE_HEIGHT / 2.0f < ball.y && player2.y < SCREEN_HEIGHT - PADDLE_HEIGHT - 1) {
                        player2.y += 0.1f;
                    }
                    if (player2.y + PADDLE_HEIGHT / 2.0f > ball.y && player2.y > 1) {
                        player2.y -= 0.1f;
                    }
                    break;
            }
            return;
        }
    }
    
    // Normal AI behavior (but only react every few frames and move slower)
    if (ai_reaction_counter >= AI_REACTION_DELAY) {
        ai_reaction_counter = 0;
        
        float paddle_center = player2.y + PADDLE_HEIGHT / 2.0f;
        float ai_speed = 0.25f * AI_SPEED_REDUCTION; // Reduced AI speed
        
        // Add some randomness to AI movement even when not missing
        if (simple_rand() % 10 == 0) {
            ai_speed *= 0.5f; // Sometimes move even slower
        }
        
        // Fixed boundary checks - AI can now move to the entire screen
        if (paddle_center < ball.y && player2.y < SCREEN_HEIGHT - PADDLE_HEIGHT - 1) {
            player2.y += ai_speed;
        }
        if (paddle_center > ball.y && player2.y > 1) { // Changed this line - was preventing upward movement
            player2.y -= ai_speed;
        }
    }
}


/**
 * @brief Main game loop update function.
 */
void pong_update() {
    if (!game_is_active) return;
    
    // Slow down the game by only updating every few timer ticks
    frame_counter++;
    if (frame_counter < 3) { // Adjust this value to change game speed
        return;
    }
    frame_counter = 0;

    // --- Ball Movement ---
    ball.x += ball.vx;
    ball.y += ball.vy;

    // --- AI Paddle Movement (with imperfections) ---
    update_ai_paddle();

    // --- Collision Detection ---
    // Top and bottom walls
    if (ball.y <= 1 || ball.y >= SCREEN_HEIGHT - 2) {
        ball.vy = -ball.vy;
    }

    // Player 1 paddle collision
    if (ball.vx < 0 && (int)ball.x <= (int)player1.x + 1 && (int)ball.x >= (int)player1.x &&
        (int)ball.y >= (int)player1.y && (int)ball.y < (int)player1.y + PADDLE_HEIGHT) {
        ball.vx = -ball.vx;
        // Add some angle based on where the ball hits the paddle
        float hit_pos = ((int)ball.y - (int)player1.y) / (float)PADDLE_HEIGHT;
        ball.vy += (hit_pos - 0.5f) * 0.3f;
    }

    // Player 2 paddle collision
    if (ball.vx > 0 && (int)ball.x >= (int)player2.x - 1 && (int)ball.x <= (int)player2.x &&
        (int)ball.y >= (int)player2.y && (int)ball.y < (int)player2.y + PADDLE_HEIGHT) {
        ball.vx = -ball.vx;
        // Add some angle based on where the ball hits the paddle
        float hit_pos = ((int)ball.y - (int)player2.y) / (float)PADDLE_HEIGHT;
        ball.vy += (hit_pos - 0.5f) * 0.3f;
    }
    
    // --- Scoring ---
    // Player 2 scores
    if (ball.x < 0) {
        score2++;
        reset_ball();
    }
    // Player 1 scores
    if (ball.x >= SCREEN_WIDTH) {
        score1++;
        reset_ball();
    }
    
    // --- Render Frame ---
    pong_render();
}

/**
 * @brief Checks if the game is currently running.
 */
bool is_pong_running() {
    return game_is_active;
}

