/* Based on node-formidable by Felix Geisendörfer 
 * Igor Afonov - afonov@gmail.com - 2012
 * MIT License - http://www.opensource.org/licenses/mit-license.php
 */

#include "multipart_parser.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void multipart_log(const char * format, ...)
{
#ifdef DEBUG_MULTIPART
    va_list args;
    va_start(args, format);

    fprintf(stderr, "[HTTP_MULTIPART_PARSER] %s:%d: ", __FILE__, __LINE__);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
#endif
}

#define NOTIFY_CB(FOR)                                                 \
do {                                                                   \
  if (p->_s->settings->on_##FOR) {                                     \
    if (p->_s->settings->on_##FOR(p) != 0) {                           \
      return i;                                                        \
    }                                                                  \
  }                                                                    \
} while (0)

#define EMIT_DATA_CB(FOR)                                              \
do {                                                                   \
  if (p->_s->settings->on_##FOR) {                                     \
    if (p->_s->settings->on_##FOR(p, (buf + mark), (i - mark)) != 0) { \
      return i;                                                        \
    }                                                                  \
  }                                                                    \
} while (0)

#define EMIT_PART_DATA_CB(FOR)                                         \
do {                                                                   \
  if (p->_s->settings->on_##FOR) {                                     \
    p->_s->parsed = p->_s->parsed + (i - mark);                        \
    if (p->_s->settings->on_##FOR(p, (buf + mark), (i - mark)) != 0) { \
      return i;                                                        \
    }                                                                  \
  }                                                                    \
} while (0)

#define LF 10
#define CR 13

struct multipart_parser_state {
  size_t index;
  size_t parsed;
  size_t boundary_length;

  int flags;

  unsigned char state;

  const multipart_parser_settings* settings;

  char* lookbehind;
  char multipart_boundary[1];
};

enum state {
  s_uninitialized = 1,
  s_start,
  s_start_boundary,
  s_header_field_start,
  s_header_field,
  s_headers_almost_done,
  s_header_value_start,
  s_header_value,
  s_header_value_almost_done,
  s_part_data_start,
  s_part_data,
  s_end
};

enum parser_flags {
  f_part_boundary = 1,
  f_last_boundary
};

multipart_parser* init_multipart_parser
    (const char *boundary, const multipart_parser_settings* settings) {

  multipart_parser* p = malloc(sizeof(multipart_parser) +
                               sizeof(multipart_parser_state) +
                               strlen(boundary) +
                               strlen(boundary) + 9);

  p->_s = (multipart_parser_state *) (p + 1);

  strcpy(p->_s->multipart_boundary, boundary);
  p->_s->boundary_length = strlen(boundary);
  
  p->_s->lookbehind = (p->_s->multipart_boundary + p->_s->boundary_length + 1);

  p->_s->index = 0;
  p->_s->state = s_start;
  p->_s->settings = settings;
  p->_s->flags = 0;
  p->_s->parsed = 0;
  return p;
}

void free_multipart_parser(multipart_parser* p) {
  free(p);
}

size_t multipart_parser_execute(multipart_parser* p, const char *buf, size_t len) {
  size_t i;
  size_t mark = 0;
  size_t prev_index = 0;
  size_t adjustment;
  char c, cl;

  for (i = 0; i < len; i++) {
    c = buf[i];
    switch (p->_s->state) {
      case s_start:
        multipart_log("s_start");
        p->_s->index = 0;
        p->_s->state = s_start_boundary;
        break;
      case s_start_boundary:
        multipart_log("s_start_boundary");
        if (p->_s->index == p->_s->boundary_length - 1) {
          if (c != CR) {
            return i;
          }
          p->_s->index++;
          break;
        } else if (p->_s->index == p->_s->boundary_length) {
          if (c != LF) {
            return i;
          }
          p->_s->index = 0;
          NOTIFY_CB(part_data_begin);
          p->_s->state = s_header_field_start;
          break;
        }

        if (c != p->_s->multipart_boundary[p->_s->index + 1]) {
          return i;
        }

        p->_s->index++;
        break;
      case s_header_field_start:
        multipart_log("s_header_field_start");
        mark = i;
        p->_s->state = s_header_field;
      case s_header_field:
        multipart_log("s_header_field");
        if (c == CR) {
          p->_s->state = s_headers_almost_done;
          break;
        }

        if (c == '-') {
          break;
        }

        if (c == ':') {
          EMIT_DATA_CB(header_field);
          p->_s->state = s_header_value_start;
          break;
        }

        cl = tolower(c);
        if (cl < 'a' || cl > 'z') {
          multipart_log("invalid character in header name");
          return i;
        }

        if (i == len - 1) EMIT_DATA_CB(header_field);
        break;
      case s_headers_almost_done:
        multipart_log("s_headers_almost_done");
        if (c != LF) {
          return i;
        }

        p->_s->state = s_part_data_start;
        break;
      case s_header_value_start:
        multipart_log("s_header_value_start");
        if (c == ' ') {
          break;
        }

        mark = i;
        p->_s->state = s_header_value;
      case s_header_value:
        multipart_log("s_header_value");
        if (c == CR) {
          EMIT_DATA_CB(header_value);
          p->_s->state = s_header_value_almost_done;
        }

        if (i == len - 1) EMIT_DATA_CB(header_value);
        break;
      case s_header_value_almost_done:
        multipart_log("s_header_value_almost_done");
        if (c != LF) {
          return i;
        }
        p->_s->state = s_header_field_start;
        break;
      case s_part_data_start:
        multipart_log("s_part_data_start");
        NOTIFY_CB(headers_complete);
        mark = i;
        p->_s->state = s_part_data;
      case s_part_data:
        multipart_log("s_part_data");
        prev_index = p->_s->index;

        if (p->_s->index < p->_s->boundary_length) {
          if (p->_s->multipart_boundary[p->_s->index] == c) {
            if (p->_s->index == 0) {
              /* very ugly way to omit emitting trailing CR+LF which doesn't belong to file but
               * rather belong to multipart stadard
               */
              adjustment = 0;
              if (buf[i - 1] == LF && buf[i - 2] == CR && buf[i + 1] == '-') {
                adjustment = 2;
              }

              i = i - adjustment;
              EMIT_PART_DATA_CB(part_data);
              i = i + adjustment;
            }
            p->_s->index++;
          } else {
            p->_s->index = 0;
          }
        } else if (p->_s->index == p->_s->boundary_length) {
          p->_s->index++;
          if (c == CR) {
            /* CR = part boundary */
            p->_s->flags |= f_part_boundary;
          } else if (c == '-') {
            /* HYPHEN = end boundary */
            p->_s->flags |= f_last_boundary;
          } else {
            p->_s->index = 0;
          }
        } else if (p->_s->index - 1 == p->_s->boundary_length)  {
          if (p->_s->flags & f_part_boundary) {
            p->_s->index = 0;
            if (c == LF) {
              /* unset the PART_BOUNDARY flag */
              p->_s->flags &= ~f_part_boundary;
              NOTIFY_CB(part_data_end);
              NOTIFY_CB(part_data_begin);
              p->_s->state = s_header_field_start;
              break;
            }
          } else if (p->_s->flags & f_last_boundary) {
            if (c == '-') {
              p->_s->index++;
            } else {
              p->_s->index = 0;
            }
          } else {
            p->_s->index = 0;
          }
        } else if (p->_s->index - 2 == p->_s->boundary_length)  {
          if (c == CR) {
            p->_s->index++;
          } else {
            p->_s->index = 0;
          }
        } else if (p->_s->index - p->_s->boundary_length == 3)  {
          p->_s->index = 0;
          if (c == LF) {
            NOTIFY_CB(part_data_end);
            NOTIFY_CB(body_end);
            p->_s->state = s_end;
            break;
          }
        }

        if (p->_s->index > 0) {
          p->_s->lookbehind[p->_s->index - 1] = c;
        } else if (prev_index > 0) {
          p->_s->parsed = p->_s->parsed + prev_index;
          p->_s->settings->on_part_data(p, p->_s->lookbehind, prev_index);
          prev_index = 0;
          mark = i;
        }

        if (p->_s->index == 0 && i == len - 1) {
          i++;
          EMIT_PART_DATA_CB(part_data);
          i--; /* otherwise this method returns len+1 */
        }
        break;
      case s_end:
        multipart_log("s_end");
        break;
      default:
        multipart_log("Multipart parser unrecoverable error");
        return 0;
    }
  }

  return i;
}
