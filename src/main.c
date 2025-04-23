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
#include "mzalloc.h"

#define NUM_TABS 12
// Limit the size of virtual sequences to 1kB to prevent overstacking.
#define SEQ_MAX 1024

// TODO: Maybe lines structure should be in every buffer and alongside the sequences.
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
    int handle;
    char* data;
    size_t used;
    size_t free;
    int grow;
    int isModified : 1;
    /// @brief Buffer is pending for new line update.
    int isPending : 1;
    // TODO: Consider raw buffers for each buffer?
    // TODO: Go over this structure and see how I can improve this.
    // Virtual buffer containing all unwritten sequences.
    struct Sequence seqs[];
};

struct Line
{
    int pos;
    int length;
};

static struct termios orig;
static struct Map binds;
static struct Buffer tabs[NUM_TABS];

/// @brief Index of the currently focused tab.
static int focus = 0;
/// @brief Cursor position.
static int vx = 0, vy = 0;
/// @brief Padding dimensions.
static int px = 0, py = 1;
/// @brief Current index into the rendering buffer.
static int pos = 0;
/// @brief Number of rows and columns present in the current window.
static int rows, cols;
/// @brief Line buffer.
// TODO: Explain the buffers and write it all out to make sure it isn't redundant.
static struct Line* lines;
/// @brief Number of lines in line buffer.
static int numLines;
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

// TODO: Be able to check if the file has been externally modified, requires currently mapping to change.
void updateLineBuffer()
{
    // TODO: Other tabs being created will fuck up the raw buffer.
    // TODO: Do I need tab rendering buffers and a global rendering buffer or something?
    if (tabs[focus].isPending == 0)
        return;

    char* tmp = raw;
    // TODO: Tab selection and possibly make the rendering more compartmentalized?
    raw += py * cols;
    int _pos = -1;
    numLines = rows - py;

    for (int i = 0; i < rows - py; i++)
    {
        int sentinel = ++_pos;
        if (_pos >= tabs[focus].used || tabs[focus].data[_pos] == '\0')
            break;

        lines[i].pos = sentinel;
        lines[i].length = 0;

        while (_pos < tabs[focus].used && tabs[focus].data[_pos] != '\n' && tabs[focus].data[_pos] != '\0')
        {
            // if (tabs[focus].data[_pos] == '\t')
            // {
            //     memcpy(raw, tabs[focus].data + sentinel, _pos - sentinel);
            //     raw += _pos - sentinel;
            //     sentinel = _pos + 1;
            //     // TODO: This doesn't work, these need to be more than just visual also visual stuff needs worked out.
            // }

            _pos++;
            lines[i].length++;
        }

        if (_pos != sentinel)
            memcpy(raw, tabs[focus].data + sentinel, _pos - sentinel);
        raw += _pos - sentinel;

        memset(raw, ' ', cols - lines[i].length);
        raw += cols - lines[i].length;
    }

    // Alarm doesn't mess with the sequence so it's being used as a placeholder, viable digits replace the alarm characters.
    strcpy(raw, "\x1b[\a\a\a\a;\a\a\a\aH");
    raw += 5;

    int row = vy + py + 1;
    int col = vx + px + 1;
    int digits = 0;
    while (row > 0)
    {
        *raw-- = '0' + (row % 10);
        row /= 10;
        digits++;
    }

    raw += digits + 5;

    digits = 0;
    while (col > 0)
    {
        *raw-- = '0' + (col % 10);
        col /= 10;
        digits++;
    }

    raw = tmp;
}

void clearScreen()
{
    // Clear the screen and return the cursor to home position.
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    updateLineBuffer();
    write(STDOUT_FILENO, raw, RAW_BUFFER_SIZE);
}

void down()
{
    if (vy + 1 >= numLines)
        return;

    vy++;
    if (vx < lines[vy].length)
        pos = lines[vy].pos + vx;
    else
    {
        vx = lines[vy].length > 0 ? lines[vy].length - 1 : 0;
        pos = lines[vy].pos + lines[vy].length - 1;
    }
}

void right()
{
    if (vx + 1 >= lines[vy].length)
    {
        if (vy + 1 < numLines)
            vx = lines[vy + 1].length;
        down();
        return;
    }

    vx++;
    pos++;
}

void up()
{
    if (vy - 1 < 0)
            return;

    vy--;
    if (vx < lines[vy].length)
        pos = lines[vy].pos + vx;
    else
    {
        vx = lines[vy].length > 0 ? lines[vy].length - 1 : 0;
        pos = lines[vy].pos + lines[vy].length - 1;
    }
}

void left()
{
    if (vx - 1 < 0)
    {
        if (vy - 1 < numLines)
            vx = lines[vy - 1].length;
        up();
        return;
    }

    vx--;
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

    // TODO: I feel like there MUST be something I'm missing. Review rendering stuff later.
    tabs[focus].isPending = -1;

    void (*func)(void);
    if ((func = map_get(&binds, rapidhash(seq, 4))))
        func();
    else
    {
        //tabs[focus].data[pos] = seq[0];
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

void buffer_open(char* path)
{
    struct Buffer buf = {
        .grow = 64,
        .free = 64,
        .isModified = 0,
        .isPending = -1
    };
    // TODO: Error handling.
    buf.handle = open(path, O_RDWR);
    buf.used = lseek(buf.handle, 0, SEEK_END);
    // TODO: This is a raw buffer and should be discarded if changes aren't saved.
    // TODO: Ideally we shouldn't take exclusive access of the file but I haven't yet figured out an alternative.
    buf.data = mmap(NULL, buf.used, PROT_READ | PROT_WRITE, MAP_SHARED, buf.handle, 0);

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

        if (tabs[i].isModified)
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
    printf("%Ix\n", mzalloc(32));
    printf("%Ix\n", mzalloc(32));
    printf("%Ix\n", mzalloc(32));

    if (getBounds(&rows, &cols) == -1)
    {
        printf("Failed to get window size.");
        return 0;
    }

    if (RAW_BUFFER_SIZE <= 1024 * 1024)
    {
        raw = alloca(RAW_BUFFER_SIZE);
        // TODO: Technically this won't work and it needs to be 8 times bigger because every character could be a newline.
        lines = alloca(RAW_BUFFER_SIZE);
    }
    else
    {
        raw = malloc(RAW_BUFFER_SIZE);
        lines = malloc(RAW_BUFFER_SIZE);
    }

    memset(raw, 0, RAW_BUFFER_SIZE);
    // No need to zero line buffer because it should never be used before lines are updated at least once.

    char* path = argc >= 2 ? argv[1] : NULL;
    while (validate_safe(&path) != 0)
    {
        if (path != NULL)
            free(path);

        path = calloc(PATH_MAX, sizeof(char));
        printf("Failed to comprehend path, enter fallback file or <^C>: ");
        scanf("%s", path);
    }

    // TODO: This looks gross, I mix camelcase and snakecase and lowercase.
    enableRawMode();
    buffer_open(path);
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
        clearScreen();
        processKeys();
    }

    return 0;
}
