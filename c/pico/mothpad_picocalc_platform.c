#include "mothpad_picocalc_platform.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <ctype.h>
#include <string.h>

#define LCD_SPI       spi1
#define LCD_SCK_PIN   10
#define LCD_MOSI_PIN  11
#define LCD_MISO_PIN  12
#define LCD_CS_PIN    13
#define LCD_DC_PIN    14
#define LCD_RST_PIN   15

#define KBD_I2C       i2c1
#define KBD_SDA_PIN   6
#define KBD_SCL_PIN   7
#define KBD_ADDR      0x1f
#define KBD_REG_KEY   0x09
#define KBD_REG_BAT   0x0b
#define KBD_CTRL_1    0x7e
#define KBD_CTRL_2    0xa5

#define GLYPH_WIDTH   5
#define GLYPH_HEIGHT  7
#define CELL_WIDTH    8
#define CELL_HEIGHT   12

static short g_lcd_x;
static short g_lcd_y;
static int g_lcd_fg = PICOCALC_COLOR_WHITE;
static int g_lcd_bg = PICOCALC_COLOR_BLACK;
static int g_color_order = PICOCALC_COLOR_ORDER_RGB;
static int g_pixel_bits = 18;
static int g_ctrl_held;

static void lcd_select(void)
{
    gpio_put(LCD_CS_PIN, 0);
}

static void lcd_deselect(void)
{
    gpio_put(LCD_CS_PIN, 1);
}

static void lcd_spi_finish(void)
{
    while(spi_is_readable(LCD_SPI))
    {
        (void)spi_get_hw(LCD_SPI)->dr;
    }
    while(spi_get_hw(LCD_SPI)->sr & SPI_SSPSR_BSY_BITS)
    {
        tight_loop_contents();
    }
    while(spi_is_readable(LCD_SPI))
    {
        (void)spi_get_hw(LCD_SPI)->dr;
    }
    spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;
}

static void lcd_write_command(uint8_t command)
{
    gpio_put(LCD_DC_PIN, 0);
    lcd_select();
    spi_write_blocking(LCD_SPI, &command, 1);
    lcd_spi_finish();
    lcd_deselect();
}

static void lcd_write_data(const uint8_t *data, size_t len)
{
    if(!data || len == 0) return;
    gpio_put(LCD_DC_PIN, 1);
    lcd_select();
    spi_write_blocking(LCD_SPI, data, len);
    lcd_spi_finish();
    lcd_deselect();
}

static void lcd_write_data_byte(uint8_t value)
{
    lcd_write_data(&value, 1);
}

static void lcd_write_u16(uint16_t value)
{
    uint8_t data[2] = {
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xff),
    };
    lcd_write_data(data, sizeof(data));
}

static void lcd_set_window(int x, int y, int w, int h)
{
    int x2 = x + w - 1;
    int y2 = y + h - 1;

    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(x2 >= PICOCALC_LCD_WIDTH) x2 = PICOCALC_LCD_WIDTH - 1;
    if(y2 >= PICOCALC_LCD_HEIGHT) y2 = PICOCALC_LCD_HEIGHT - 1;

    lcd_write_command(0x2a);
    lcd_write_u16((uint16_t)x);
    lcd_write_u16((uint16_t)x2);

    lcd_write_command(0x2b);
    lcd_write_u16((uint16_t)y);
    lcd_write_u16((uint16_t)y2);

    lcd_write_command(0x2c);
}

static void lcd_ordered_rgb(int color, uint8_t out[3])
{
    uint8_t r = (uint8_t)((color >> 16) & 0xff);
    uint8_t g = (uint8_t)((color >> 8) & 0xff);
    uint8_t b = (uint8_t)(color & 0xff);

    switch(g_color_order)
    {
        case PICOCALC_COLOR_ORDER_RBG: out[0] = r; out[1] = b; out[2] = g; break;
        case PICOCALC_COLOR_ORDER_GRB: out[0] = g; out[1] = r; out[2] = b; break;
        case PICOCALC_COLOR_ORDER_GBR: out[0] = g; out[1] = b; out[2] = r; break;
        case PICOCALC_COLOR_ORDER_BRG: out[0] = b; out[1] = r; out[2] = g; break;
        case PICOCALC_COLOR_ORDER_BGR: out[0] = b; out[1] = g; out[2] = r; break;
        case PICOCALC_COLOR_ORDER_RGB:
        default:                       out[0] = r; out[1] = g; out[2] = b; break;
    }
}

static int lcd_color_bytes(int color, uint8_t out[3])
{
    uint8_t rgb[3];
    lcd_ordered_rgb(color, rgb);
    if(g_pixel_bits == 16)
    {
        uint16_t packed =
            (uint16_t)(((rgb[0] & 0xf8) << 8) |
                       ((rgb[1] & 0xfc) << 3) |
                       (rgb[2] >> 3));
        out[0] = (uint8_t)(packed >> 8);
        out[1] = (uint8_t)(packed & 0xff);
        out[2] = 0;
        return 2;
    }
    out[0] = rgb[0];
    out[1] = rgb[1];
    out[2] = rgb[2];
    return 3;
}

static void lcd_fill_rect(int x, int y, int w, int h, int color)
{
    uint8_t row[PICOCALC_LCD_WIDTH * 3];
    uint8_t rgb[3];
    int pixel_bytes;

    if(w <= 0 || h <= 0) return;
    if(x < 0 || y < 0 || x >= PICOCALC_LCD_WIDTH || y >= PICOCALC_LCD_HEIGHT) return;
    if(x + w > PICOCALC_LCD_WIDTH) w = PICOCALC_LCD_WIDTH - x;
    if(y + h > PICOCALC_LCD_HEIGHT) h = PICOCALC_LCD_HEIGHT - y;

    pixel_bytes = lcd_color_bytes(color, rgb);
    for(int i = 0; i < w; ++i)
    {
        row[i * pixel_bytes + 0] = rgb[0];
        row[i * pixel_bytes + 1] = rgb[1];
        if(pixel_bytes == 3) row[i * pixel_bytes + 2] = rgb[2];
    }

    lcd_set_window(x, y, w, h);
    gpio_put(LCD_DC_PIN, 1);
    lcd_select();
    for(int yy = 0; yy < h; ++yy)
    {
        spi_write_blocking(LCD_SPI, row, (size_t)(w * pixel_bytes));
    }
    lcd_spi_finish();
    lcd_deselect();
}

static const uint8_t *glyph5x7(char ch)
{
    unsigned char code = (unsigned char)ch;
    static const uint8_t blank[GLYPH_HEIGHT] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t unknown[GLYPH_HEIGHT] = {14, 17, 1, 2, 4, 0, 4};
    static const uint8_t menu_tl[GLYPH_HEIGHT] = {0, 0, 15, 8, 8, 8, 8};
    static const uint8_t menu_tr[GLYPH_HEIGHT] = {0, 0, 30, 2, 2, 2, 2};
    static const uint8_t menu_bl[GLYPH_HEIGHT] = {8, 8, 8, 8, 15, 0, 0};
    static const uint8_t menu_br[GLYPH_HEIGHT] = {2, 2, 2, 2, 30, 0, 0};
    static const uint8_t menu_h[GLYPH_HEIGHT] = {0, 0, 31, 0, 0, 0, 0};
    static const uint8_t menu_v[GLYPH_HEIGHT] = {2, 2, 2, 2, 2, 2, 2};
    static const uint8_t bat0[GLYPH_HEIGHT] = {0, 14, 17, 17, 17, 14, 0};
    static const uint8_t bat25[GLYPH_HEIGHT] = {0, 14, 25, 25, 25, 14, 0};
    static const uint8_t bat50[GLYPH_HEIGHT] = {0, 14, 29, 29, 29, 14, 0};
    static const uint8_t bat75[GLYPH_HEIGHT] = {0, 14, 31, 31, 31, 14, 0};
    static const uint8_t bat100[GLYPH_HEIGHT] = {0, 31, 31, 31, 31, 31, 0};
    static const uint8_t bang[GLYPH_HEIGHT] = {4, 4, 4, 4, 4, 0, 4};
    static const uint8_t quote[GLYPH_HEIGHT] = {10, 10, 10, 0, 0, 0, 0};
    static const uint8_t hash[GLYPH_HEIGHT] = {18, 18, 63, 18, 63, 18, 18};
    static const uint8_t dollar[GLYPH_HEIGHT] = {4, 15, 20, 14, 5, 30, 4};
    static const uint8_t percent[GLYPH_HEIGHT] = {24, 25, 2, 4, 8, 19, 3};
    static const uint8_t amp[GLYPH_HEIGHT] = {12, 18, 20, 8, 21, 18, 13};
    static const uint8_t apos[GLYPH_HEIGHT] = {4, 4, 8, 0, 0, 0, 0};
    static const uint8_t lpar[GLYPH_HEIGHT] = {2, 4, 8, 8, 8, 4, 2};
    static const uint8_t rpar[GLYPH_HEIGHT] = {8, 4, 2, 2, 2, 4, 8};
    static const uint8_t star[GLYPH_HEIGHT] = {0, 4, 21, 14, 21, 4, 0};
    static const uint8_t plus[GLYPH_HEIGHT] = {0, 4, 4, 31, 4, 4, 0};
    static const uint8_t comma[GLYPH_HEIGHT] = {0, 0, 0, 0, 4, 4, 8};
    static const uint8_t minus[GLYPH_HEIGHT] = {0, 0, 0, 31, 0, 0, 0};
    static const uint8_t dot[GLYPH_HEIGHT] = {0, 0, 0, 0, 0, 12, 12};
    static const uint8_t slash[GLYPH_HEIGHT] = {0, 1, 2, 4, 8, 16, 0};
    static const uint8_t colon[GLYPH_HEIGHT] = {0, 12, 12, 0, 12, 12, 0};
    static const uint8_t semi[GLYPH_HEIGHT] = {0, 12, 12, 0, 4, 4, 8};
    static const uint8_t lt[GLYPH_HEIGHT] = {2, 4, 8, 16, 8, 4, 2};
    static const uint8_t eq[GLYPH_HEIGHT] = {0, 0, 31, 0, 31, 0, 0};
    static const uint8_t gt[GLYPH_HEIGHT] = {8, 4, 2, 1, 2, 4, 8};
    static const uint8_t at[GLYPH_HEIGHT] = {30, 33, 45, 43, 46, 32, 30};
    static const uint8_t lb[GLYPH_HEIGHT] = {14, 8, 8, 8, 8, 8, 14};
    static const uint8_t backslash[GLYPH_HEIGHT] = {0, 16, 8, 4, 2, 1, 0};
    static const uint8_t rb[GLYPH_HEIGHT] = {14, 2, 2, 2, 2, 2, 14};
    static const uint8_t caret[GLYPH_HEIGHT] = {4, 10, 17, 0, 0, 0, 0};
    static const uint8_t under[GLYPH_HEIGHT] = {0, 0, 0, 0, 0, 0, 31};
    static const uint8_t tick[GLYPH_HEIGHT] = {8, 4, 2, 0, 0, 0, 0};
    static const uint8_t lc[GLYPH_HEIGHT] = {6, 8, 8, 16, 8, 8, 6};
    static const uint8_t bar[GLYPH_HEIGHT] = {4, 4, 4, 0, 4, 4, 4};
    static const uint8_t rc[GLYPH_HEIGHT] = {12, 2, 2, 1, 2, 2, 12};
    static const uint8_t tilde[GLYPH_HEIGHT] = {0, 0, 0, 8, 21, 2, 0};
    static const uint8_t d0[GLYPH_HEIGHT] = {14, 17, 19, 21, 25, 17, 14};
    static const uint8_t d1[GLYPH_HEIGHT] = {4, 12, 4, 4, 4, 4, 14};
    static const uint8_t d2[GLYPH_HEIGHT] = {14, 17, 1, 2, 4, 8, 31};
    static const uint8_t d3[GLYPH_HEIGHT] = {30, 1, 1, 14, 1, 1, 30};
    static const uint8_t d4[GLYPH_HEIGHT] = {2, 6, 10, 18, 31, 2, 2};
    static const uint8_t d5[GLYPH_HEIGHT] = {31, 16, 16, 30, 1, 1, 30};
    static const uint8_t d6[GLYPH_HEIGHT] = {6, 8, 16, 30, 17, 17, 14};
    static const uint8_t d7[GLYPH_HEIGHT] = {31, 1, 2, 4, 8, 8, 8};
    static const uint8_t d8[GLYPH_HEIGHT] = {14, 17, 17, 14, 17, 17, 14};
    static const uint8_t d9[GLYPH_HEIGHT] = {14, 17, 17, 15, 1, 2, 12};
    static const uint8_t A[GLYPH_HEIGHT] = {14, 17, 17, 31, 17, 17, 17};
    static const uint8_t B[GLYPH_HEIGHT] = {30, 17, 17, 30, 17, 17, 30};
    static const uint8_t C[GLYPH_HEIGHT] = {14, 17, 16, 16, 16, 17, 14};
    static const uint8_t D[GLYPH_HEIGHT] = {30, 17, 17, 17, 17, 17, 30};
    static const uint8_t E[GLYPH_HEIGHT] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t F[GLYPH_HEIGHT] = {31, 16, 16, 30, 16, 16, 16};
    static const uint8_t G[GLYPH_HEIGHT] = {14, 17, 16, 23, 17, 17, 14};
    static const uint8_t H[GLYPH_HEIGHT] = {17, 17, 17, 31, 17, 17, 17};
    static const uint8_t I[GLYPH_HEIGHT] = {14, 4, 4, 4, 4, 4, 14};
    static const uint8_t J[GLYPH_HEIGHT] = {7, 2, 2, 2, 2, 18, 12};
    static const uint8_t K[GLYPH_HEIGHT] = {17, 18, 20, 24, 20, 18, 17};
    static const uint8_t L[GLYPH_HEIGHT] = {16, 16, 16, 16, 16, 16, 31};
    static const uint8_t M[GLYPH_HEIGHT] = {17, 27, 21, 21, 17, 17, 17};
    static const uint8_t N[GLYPH_HEIGHT] = {17, 25, 21, 19, 17, 17, 17};
    static const uint8_t O[GLYPH_HEIGHT] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t P[GLYPH_HEIGHT] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t Q[GLYPH_HEIGHT] = {14, 17, 17, 17, 21, 18, 13};
    static const uint8_t R[GLYPH_HEIGHT] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t S[GLYPH_HEIGHT] = {15, 16, 16, 14, 1, 1, 30};
    static const uint8_t T[GLYPH_HEIGHT] = {31, 4, 4, 4, 4, 4, 4};
    static const uint8_t U[GLYPH_HEIGHT] = {17, 17, 17, 17, 17, 17, 14};
    static const uint8_t V[GLYPH_HEIGHT] = {17, 17, 17, 17, 17, 10, 4};
    static const uint8_t W[GLYPH_HEIGHT] = {17, 17, 17, 21, 21, 21, 10};
    static const uint8_t X[GLYPH_HEIGHT] = {17, 17, 10, 4, 10, 17, 17};
    static const uint8_t Y[GLYPH_HEIGHT] = {17, 17, 10, 4, 4, 4, 4};
    static const uint8_t Z[GLYPH_HEIGHT] = {31, 1, 2, 4, 8, 16, 31};
    static const uint8_t a[GLYPH_HEIGHT] = {0, 0, 14, 1, 15, 17, 15};
    static const uint8_t b[GLYPH_HEIGHT] = {16, 16, 22, 25, 17, 17, 30};
    static const uint8_t c[GLYPH_HEIGHT] = {0, 0, 14, 16, 16, 17, 14};
    static const uint8_t d[GLYPH_HEIGHT] = {1, 1, 13, 19, 17, 17, 15};
    static const uint8_t e[GLYPH_HEIGHT] = {0, 0, 14, 17, 31, 16, 14};
    static const uint8_t f[GLYPH_HEIGHT] = {6, 8, 8, 28, 8, 8, 8};
    static const uint8_t g[GLYPH_HEIGHT] = {0, 0, 15, 17, 15, 1, 14};
    static const uint8_t h[GLYPH_HEIGHT] = {16, 16, 22, 25, 17, 17, 17};
    static const uint8_t i[GLYPH_HEIGHT] = {4, 0, 12, 4, 4, 4, 14};
    static const uint8_t j[GLYPH_HEIGHT] = {2, 0, 6, 2, 2, 18, 12};
    static const uint8_t k[GLYPH_HEIGHT] = {16, 16, 18, 20, 24, 20, 18};
    static const uint8_t l[GLYPH_HEIGHT] = {12, 4, 4, 4, 4, 4, 14};
    static const uint8_t m[GLYPH_HEIGHT] = {0, 0, 26, 21, 21, 17, 17};
    static const uint8_t n[GLYPH_HEIGHT] = {0, 0, 22, 25, 17, 17, 17};
    static const uint8_t o[GLYPH_HEIGHT] = {0, 0, 14, 17, 17, 17, 14};
    static const uint8_t p[GLYPH_HEIGHT] = {0, 0, 30, 17, 30, 16, 16};
    static const uint8_t q[GLYPH_HEIGHT] = {0, 0, 15, 17, 15, 1, 1};
    static const uint8_t r[GLYPH_HEIGHT] = {0, 0, 22, 25, 16, 16, 16};
    static const uint8_t s[GLYPH_HEIGHT] = {0, 0, 15, 16, 14, 1, 30};
    static const uint8_t t[GLYPH_HEIGHT] = {8, 8, 28, 8, 8, 9, 6};
    static const uint8_t u[GLYPH_HEIGHT] = {0, 0, 17, 17, 17, 19, 13};
    static const uint8_t v[GLYPH_HEIGHT] = {0, 0, 17, 17, 17, 10, 4};
    static const uint8_t w[GLYPH_HEIGHT] = {0, 0, 17, 17, 21, 21, 10};
    static const uint8_t x[GLYPH_HEIGHT] = {0, 0, 17, 10, 4, 10, 17};
    static const uint8_t y[GLYPH_HEIGHT] = {0, 0, 17, 17, 15, 1, 14};
    static const uint8_t z[GLYPH_HEIGHT] = {0, 0, 31, 2, 4, 8, 31};

    switch(code)
    {
        case 0x80: return menu_tl;
        case 0x81: return menu_tr;
        case 0x82: return menu_bl;
        case 0x83: return menu_br;
        case 0x84: return menu_h;
        case 0x85: return menu_v;
        case 0x86: return bat0;
        case 0x87: return bat25;
        case 0x88: return bat50;
        case 0x89: return bat75;
        case 0x8a: return bat100;
        case ' ': return blank;
        case '!': return bang;
        case '"': return quote;
        case '#': return hash;
        case '$': return dollar;
        case '%': return percent;
        case '&': return amp;
        case '\'': return apos;
        case '(': return lpar;
        case ')': return rpar;
        case '*': return star;
        case '+': return plus;
        case ',': return comma;
        case '-': return minus;
        case '.': return dot;
        case '/': return slash;
        case '0': return d0;
        case '1': return d1;
        case '2': return d2;
        case '3': return d3;
        case '4': return d4;
        case '5': return d5;
        case '6': return d6;
        case '7': return d7;
        case '8': return d8;
        case '9': return d9;
        case ':': return colon;
        case ';': return semi;
        case '<': return lt;
        case '=': return eq;
        case '>': return gt;
        case '?': return unknown;
        case '@': return at;
        case '[': return lb;
        case '\\': return backslash;
        case ']': return rb;
        case '^': return caret;
        case '_': return under;
        case '`': return tick;
        case '{': return lc;
        case '|': return bar;
        case '}': return rc;
        case '~': return tilde;
        case 'a': return a;
        case 'b': return b;
        case 'c': return c;
        case 'd': return d;
        case 'e': return e;
        case 'f': return f;
        case 'g': return g;
        case 'h': return h;
        case 'i': return i;
        case 'j': return j;
        case 'k': return k;
        case 'l': return l;
        case 'm': return m;
        case 'n': return n;
        case 'o': return o;
        case 'p': return p;
        case 'q': return q;
        case 'r': return r;
        case 's': return s;
        case 't': return t;
        case 'u': return u;
        case 'v': return v;
        case 'w': return w;
        case 'x': return x;
        case 'y': return y;
        case 'z': return z;
        default: break;
    }

    switch((char)toupper(code))
    {
        case 'A': return A;
        case 'B': return B;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'F': return F;
        case 'G': return G;
        case 'H': return H;
        case 'I': return I;
        case 'J': return J;
        case 'K': return K;
        case 'L': return L;
        case 'M': return M;
        case 'N': return N;
        case 'O': return O;
        case 'P': return P;
        case 'Q': return Q;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
        case 'V': return V;
        case 'W': return W;
        case 'X': return X;
        case 'Y': return Y;
        case 'Z': return Z;
        default: return unknown;
    }
}

void picocalc_lcd_init(void)
{
    gpio_init(LCD_SCK_PIN);
    gpio_init(LCD_MOSI_PIN);
    gpio_init(LCD_MISO_PIN);
    gpio_init(LCD_CS_PIN);
    gpio_init(LCD_DC_PIN);
    gpio_init(LCD_RST_PIN);
    gpio_set_dir(LCD_SCK_PIN, GPIO_OUT);
    gpio_set_dir(LCD_MOSI_PIN, GPIO_OUT);
    gpio_set_dir(LCD_CS_PIN, GPIO_OUT);
    gpio_set_dir(LCD_DC_PIN, GPIO_OUT);
    gpio_set_dir(LCD_RST_PIN, GPIO_OUT);

    spi_init(LCD_SPI, 25000000);
    gpio_set_function(LCD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_input_hysteresis_enabled(LCD_MISO_PIN, true);
    gpio_put(LCD_CS_PIN, 1);
    gpio_put(LCD_DC_PIN, 1);
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(10);
    gpio_put(LCD_RST_PIN, 0);
    sleep_ms(10);
    gpio_put(LCD_RST_PIN, 1);
    sleep_ms(200);

    lcd_write_command(0xe0);
    {
        const uint8_t data[] = {0x00, 0x03, 0x09, 0x08, 0x16, 0x0a, 0x3f, 0x78,
                                0x4c, 0x09, 0x0a, 0x08, 0x16, 0x1a, 0x0f};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0xe1);
    {
        const uint8_t data[] = {0x00, 0x16, 0x19, 0x03, 0x0f, 0x05, 0x32, 0x45,
                                0x46, 0x04, 0x0e, 0x0d, 0x35, 0x37, 0x0f};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0xc0);
    {
        const uint8_t data[] = {0x17, 0x15};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0xc1);
    lcd_write_data_byte(0x41);

    lcd_write_command(0xc5);
    {
        const uint8_t data[] = {0x00, 0x12, 0x80};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0x36);
    lcd_write_data_byte(0x48);

    lcd_write_command(0x3a);
    lcd_write_data_byte(0x66);

    lcd_write_command(0xb0);
    lcd_write_data_byte(0x00);

    lcd_write_command(0xb1);
    lcd_write_data_byte(0xa0);

    lcd_write_command(0x21);

    lcd_write_command(0xb4);
    lcd_write_data_byte(0x02);

    lcd_write_command(0xb6);
    {
        const uint8_t data[] = {0x02, 0x02, 0x3b};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0xb7);
    lcd_write_data_byte(0xc6);

    lcd_write_command(0xe9);
    lcd_write_data_byte(0x00);

    lcd_write_command(0xf7);
    {
        const uint8_t data[] = {0xa9, 0x51, 0x2c, 0x82};
        lcd_write_data(data, sizeof(data));
    }

    lcd_write_command(0x11);
    sleep_ms(120);
    lcd_write_command(0x29);
    sleep_ms(120);
    lcd_write_command(0x36);
    lcd_write_data_byte(0x48);
    picocalc_lcd_set_pixel_format(18);

    g_lcd_x = 0;
    g_lcd_y = 0;
}

void picocalc_lcd_clear(void)
{
    lcd_fill_rect(0, 0, PICOCALC_LCD_WIDTH, PICOCALC_LCD_HEIGHT, g_lcd_bg);
    g_lcd_x = 0;
    g_lcd_y = 0;
}

void picocalc_lcd_set_color_order(int order)
{
    if(order < PICOCALC_COLOR_ORDER_RGB || order > PICOCALC_COLOR_ORDER_BGR)
    {
        order = PICOCALC_COLOR_ORDER_RGB;
    }
    g_color_order = order;
}

void picocalc_lcd_set_panel_color_mode(int bgr, int invert)
{
    lcd_write_command(0x36);
    lcd_write_data_byte((uint8_t)(0x40 | (bgr ? 0x08 : 0x00)));
    lcd_write_command(invert ? 0x21 : 0x20);
}

void picocalc_lcd_set_pixel_format(int bits_per_pixel)
{
    if(bits_per_pixel == 16)
    {
        g_pixel_bits = 16;
        lcd_write_command(0x3a);
        lcd_write_data_byte(0x55);
    }
    else
    {
        g_pixel_bits = 18;
        lcd_write_command(0x3a);
        lcd_write_data_byte(0x66);
    }
}

void picocalc_lcd_set_cursor(short x, short y)
{
    g_lcd_x = x;
    g_lcd_y = y;
}

void picocalc_lcd_set_colors(int fg, int bg)
{
    g_lcd_fg = fg;
    g_lcd_bg = bg;
}

void picocalc_lcd_put_char(char ch, int flush)
{
    uint8_t pixels[CELL_WIDTH * CELL_HEIGHT * 3];
    uint8_t fg[3];
    uint8_t bg[3];
    const uint8_t *glyph;
    int glyph_width;
    int pixel_bytes;
    (void)flush;

    if(ch == '\t') ch = ' ';
    if((unsigned char)ch < 32) ch = ' ';

    pixel_bytes = lcd_color_bytes(g_lcd_fg, fg);
    (void)lcd_color_bytes(g_lcd_bg, bg);
    glyph = glyph5x7(ch);
    glyph_width = (ch == '@' || ch == '#') ? 6 : GLYPH_WIDTH;

    for(int y = 0; y < CELL_HEIGHT; ++y)
    {
        for(int x = 0; x < CELL_WIDTH; ++x)
        {
            int lit = 0;
            int gx = x - 1;
            int gy = y - 2;
            if(gx >= 0 && gy >= 0 && gy < GLYPH_HEIGHT)
            {
                if(gx < glyph_width)
                {
                    lit = (glyph[gy] & (1 << (glyph_width - 1 - gx))) != 0;
                }
            }
            uint8_t *dst = &pixels[(y * CELL_WIDTH + x) * pixel_bytes];
            const uint8_t *src = lit ? fg : bg;
            dst[0] = src[0];
            dst[1] = src[1];
            if(pixel_bytes == 3) dst[2] = src[2];
        }
    }

    lcd_set_window(g_lcd_x, g_lcd_y, CELL_WIDTH, CELL_HEIGHT);
    lcd_write_data(pixels, (size_t)(CELL_WIDTH * CELL_HEIGHT * pixel_bytes));
    g_lcd_x += CELL_WIDTH;
    if(g_lcd_x >= PICOCALC_LCD_WIDTH)
    {
        g_lcd_x = 0;
        g_lcd_y += CELL_HEIGHT;
    }
}

void picocalc_kbd_init(void)
{
    i2c_init(KBD_I2C, 10000);
    gpio_set_function(KBD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(KBD_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(KBD_SDA_PIN);
    gpio_pull_up(KBD_SCL_PIN);
    g_ctrl_held = 0;
}

int picocalc_kbd_read_raw(uint16_t *out)
{
    uint8_t reg = KBD_REG_KEY;
    uint16_t data = 0;

    if(i2c_write_blocking(KBD_I2C, KBD_ADDR, &reg, 1, true) != 1) return -1;
    sleep_ms(16);
    if(i2c_read_blocking(KBD_I2C, KBD_ADDR, (uint8_t *)&data, 2, false) != 2) return -1;
    if(out) *out = data;
    return 0;
}

int picocalc_kbd_read(void)
{
    uint16_t data = 0;
    int key;
    int state;

    if(picocalc_kbd_read_raw(&data) != 0) return -1;
    if(data == 0) return -1;
    key = data >> 8;
    state = data & 0xff;
    if(key == 0 || state == 0) return -1;

    if(key == KBD_CTRL_1 || key == KBD_CTRL_2)
    {
        if(state == 1 || state == 2) g_ctrl_held = 1;
        if(state == 3) g_ctrl_held = 0;
        return -1;
    }

    if(state != 1 && state != 2) return -1;

    if(g_ctrl_held)
    {
        if(key >= 'a' && key <= 'z') return key - 'a' + 1;
        if(key >= 'A' && key <= 'Z') return key - 'A' + 1;
    }

    return key;
}

int picocalc_kbd_read_battery(int *percent, int *charging)
{
    uint8_t reg = KBD_REG_BAT;
    uint16_t data = 0;
    int raw;

    if(i2c_write_blocking(KBD_I2C, KBD_ADDR, &reg, 1, true) != 1) return -1;
    sleep_ms(16);
    if(i2c_read_blocking(KBD_I2C, KBD_ADDR, (uint8_t *)&data, 2, false) != 2) return -1;
    if(data == 0) return -1;

    raw = (data >> 8) & 0xff;
    if(charging) *charging = (raw & 0x80) != 0;
    raw &= 0x7f;
    if(raw > 100) raw = 100;
    if(percent) *percent = raw;
    return 0;
}
