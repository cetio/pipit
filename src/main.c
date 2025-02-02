#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include "map.h"
#include "alloca.h"
#include <limits.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "rapidhash.h"

#define NUM_TABS 12
// Limit the size of virtual sequences to 1kB to prevent overstacking.
#define SEQ_MAX 1024

struct Sequence
{
    // 0: addition
    // 1: deletion
    // 2: special
    int kind : 2;
    // 0: skipped when moving cursor.
    // 1: registers as only a single character when moving cursor.
    int value : 1;
    // This need not be more than 11 bits, but it's 2 spare so why not.
    int dlen : 13;
    // Horizontal index of the sequence in the relative column.
    short index;
    // Size of text that this replaces, only applicable for addition or special sequences.
    short slen;
    // This may only contain SEQ_MAX number of chars.
    char* data;
};

struct Buffer
{
    int desc;
    char* data;
    size_t used;
    size_t free;
    int grow;
    int modified;
    // Virtual buffer containing all unwritten sequences.
    struct Sequence seqs[];
};

static struct termios orig;
static struct Map binds;
static struct Buffer tabs[NUM_TABS];

/// @brief Cursor position.
static int vx = 1, vy = 1;
/// @brief Padding dimensions.
static int px = 1, py = 1;
/// @brief Current index into the rendering buffer.
static int pos;
/// @brief Number of rows and columns present in the current window.
static int rows, cols;
/// @brief Raw rendering buffer.
static char* raw;

#define BUFFER_SIZE cols * rows
// The extra 32 bytes here are to make room for the cursor sequence.
#define RAW_BUFFER_SIZE cols * rows + 32

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig);
    atexit(disableRawMode);
    struct termios raw = orig;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int getBounds(int* rows, int* cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
        return -1;

    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

void clearScreen()
{
    // Clear the screen and return the cursor to home position.
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    void* rout = raw;

    // Set the current rendering position to be after the tab component.
    // This equates to the first character after all pre-rendered rows.
    raw += py * cols;

    // TODO: Cache the results of getting all the newlines.
    // TODO: memcpy and strchr to a single loop.
    // TODO: Skip vy number of lines and go forward vx past cols.
    char* tbuf = tabs[0].data;
    // Shift down the view by all rows visible when the cursor exits the bounds.
    // TODO: Implement single-line downshift.
    int downshift = (vy / rows) * rows;
    for (int i = ((vy / rows) * rows); i > 1; i--)
        tbuf = strchr(tbuf, '\n') + 1;

    for (int i = 0; i < rows - py; i++)
    {
        char* tmp = strchr(tbuf, '\n');
        if (tmp == NULL)
        {
            // We've run out of data to show but we need to finish flushing the rest of the buffer
            // because we need the raw pointer to be at BUFFER_SIZE.
            int rem = (rows - py - i) * cols;
            memset(raw, ' ', rem);
            raw += rem;
            break;
        }

        int len = tmp - tbuf;
        memcpy(raw, tbuf, len);
        // Fill the rest of the data with spaces so it goes to the next line.
        memset(raw + len, ' ', cols - len);

        tbuf = tmp + 1;
        raw += cols;
    }

    *raw++ = '\x1b';  // Escape character
    *raw++ = '[';     // Start of CSI (Control Sequence Introducer)

    int row = vy + (py * ((downshift / rows) + 1)) - downshift;
    char buf[4]; // Buffer for digits
    int num_digits = 0;

    // Convert row to characters and store in buffer
    do {
        buf[num_digits++] = '0' + (row % 10);
        row /= 10;
    } while (row > 0);

    // Write digits in reverse order (to form the correct number)
    for (int j = num_digits - 1; j >= 0; j--)
        *raw++ = buf[j];

    *raw++ = ';'; // Separator between row and column

    // Write vx (column) part of the cursor position
    num_digits = 0;
    int col = vx;

    // Convert column to characters and store in buffer
    do {
        buf[num_digits++] = '0' + (col % 10);
        col /= 10;
    } while (col > 0);

    // Write digits in reverse order
    for (int j = num_digits - 1; j >= 0; j--)
        *raw++ = buf[j];

    *raw = 'H';

    write(STDOUT_FILENO, raw = rout, RAW_BUFFER_SIZE);
}

// TODO: Bounds checking using buffer.
void left()
{
    pos--;
    vx--;
}

void down()
{
    vy++;

    // TODO: Accomodations for end of file and preventing segfaults.
    int orig = pos;
    while (pos < tabs[0].used && tabs[0].data[pos] != '\n')
        pos++;

    int start = pos++;
    while (pos < tabs[0].used && tabs[0].data[pos] != '\n')
        pos++;

    if (pos - start + 1 >= vx)
        pos = start + vx;
    else
        vx = pos - start;
}

void right()
{
    if (tabs[0].data[pos] == '\n')
    {
        down();
        return;
    }

    pos++;
    vx++;
}

void up()
{
    vy--;

    // TODO: Accomodations for end of file and preventing segfaults.
    int orig = pos;
    while (pos > 0 && tabs[0].data[pos] != '\n')
        pos--;

    int start = pos--;
    while (pos > 0 && tabs[0].data[pos] != '\n')
        pos--;

    if (start - pos >= vx)
        pos += vx - 1;
    else
    {
        vx = start - pos;
        pos = start;
    }

    // Segfault precaution because I'm too lazy to do more fixer-upping.
    if (pos < 0)
        pos--;
}

void quit()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    exit(65);
}

void processKeys()
{
    char seq[4] = {0, 0, 0, 0};
    int len = read(STDIN_FILENO, seq, 4);

    if (len <= 0)
        return;

    if (seq[0] == 'q')
        quit();

    void (*func)(void);
    if ((func = map_get(&binds, rapidhash(seq, 4))))
        func();
    else
    {
        //tabs[0].data[pos] = seq[0];
        right();
    }
}

char* expand_path(const char* path)
{
    if (!path || !*path)
        return NULL;

    size_t buffer_size = PATH_MAX;
    char* expanded = malloc(buffer_size);

    if (!expanded)
        return NULL;

    char* dst = expanded;
    const char* src = path;

    if (*src == '~')
    {
        const char* home = NULL;

        if (*(src + 1) == '/' || *(src + 1) == '\0')
        {
            home = getenv("HOME");

            if (!home)
            {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : NULL;
            }
        }
        else
        {
            const char* user_start = src + 1;
            const char* user_end = strchr(user_start, '/');
            size_t user_len = user_end ? (size_t)(user_end - user_start) : strlen(user_start);

            char* user = malloc(user_len + 1);

            if (!user)
            {
                free(expanded);
                return NULL;
            }

            memcpy(user, user_start, user_len);
            user[user_len] = '\0';

            struct passwd* pw = getpwnam(user);
            home = pw ? pw->pw_dir : NULL;

            free(user);

            src += user_len;
        }

        if (!home)
        {
            free(expanded);
            return NULL;
        }

        size_t home_len = strlen(home);

        if (home_len >= buffer_size)
        {
            free(expanded);
            return NULL;
        }

        memcpy(dst, home, home_len);
        dst += home_len;
        src++;
    }

    while (*src)
    {
        if (*src == '$')
        {
            const char* var_start = src + 1;
            const char* var_end = var_start;

            while (isalnum(*var_end) || *var_end == '_')
                var_end++;

            if (var_start == var_end)
            {
                *dst++ = *src++;
                continue;
            }

            size_t var_len = (size_t)(var_end - var_start);
            char* var_name = malloc(var_len + 1);

            if (!var_name)
            {
                free(expanded);
                return NULL;
            }

            memcpy(var_name, var_start, var_len);
            var_name[var_len] = '\0';

            const char* val = getenv(var_name);

            free(var_name);

            if (val)
            {
                size_t val_len = strlen(val);

                if ((dst - expanded) + val_len >= buffer_size)
                {
                    free(expanded);
                    return NULL;
                }

                memcpy(dst, val, val_len);
                dst += val_len;
            }

            src = var_end;
        }
        else
        {
            if ((dst - expanded) + 1 >= buffer_size)
            {
                free(expanded);
                return NULL;
            }

            *dst++ = *src++;
        }
    }

    *dst = '\0';
    return expanded;
}

int validate(char* path)
{
    return access(path, F_OK);
}

int validate_safe(char** path)
{
    *path = expand_path(*path);
    return validate(*path);
}

void bopen(char* path)
{
    struct Buffer buf = {
        .grow = 64,
        .free = 64,
        .modified = 0
    };
    // TODO: Error handling.
    buf.desc = open(path, O_RDWR);
    buf.used = lseek(buf.desc, 0, SEEK_END);
    buf.data = mmap(NULL, buf.used, PROT_READ | PROT_WRITE, MAP_SHARED, buf.desc, 0);

    char* name = strrchr(path, '/');
    if (name != path)
        name++;

    for (int i = 0; i < NUM_TABS; i++)
    {
        if (tabs[i].data != NULL)
            continue;

        tabs[i] = buf;

        char* ptr = strchr(raw, '\a');
        ptr = ptr == NULL ? raw : ptr;
        int len = ptr - raw;

        if (len > 0)
            *ptr++ = ' ';

        len += 3;
        *ptr++ = '1' + i;
        *ptr++ = ' ';

        while (*name != '\0')
        {
            *ptr++ = *name++;
            len++;
        }

        if (tabs[i].modified)
        {
            *ptr++ = '*';
            len++;
        }

        *ptr = '\a';

        int rem = cols - (len % cols) + 1;
        memset(ptr, ' ', rem);

        py += len / cols;
        break;
    }
}

int main(int argc, char** argv)
{
    if (getBounds(&rows, &cols) == -1)
    {
        printf("Failed to get window size.");
        return 0;
    }

    if (RAW_BUFFER_SIZE <= 1024 * 1024)
        raw = alloca(RAW_BUFFER_SIZE);
    else
        raw = malloc(RAW_BUFFER_SIZE);

    memset(raw, 0, RAW_BUFFER_SIZE);

    char* path = argc >= 2 ? argv[1] : NULL;
    while (validate_safe(&path) != 0)
    {
        if (path != NULL)
            free(path);

        path = calloc(PATH_MAX, sizeof(char));
        printf("Failed to comprehend path, enter fallback file or <^C>: ");
        scanf("%s", path);
    }

    enableRawMode();
    bopen(path);
    map_init(&binds);

    map_set(&binds, rapidhash("\x1b[A", sizeof("\x1b[A")), &up);
    map_set(&binds, rapidhash("\x1b[D", sizeof("\x1b[D")), &left);
    map_set(&binds, rapidhash("\x1b[B", sizeof("\x1b[B")), &down);
    map_set(&binds, rapidhash("\x1b[C", sizeof("\x1b[C")), &right);
    map_set(&binds, rapidhash("\x18", sizeof("\x18")), &quit);
    map_set(&binds, rapidhash("^[OQ", sizeof("^[OQ")), &quit);
    //return 0;

    while (1)
    {
        //clearScreen();
        processKeys();
    }

    return 0;
}
