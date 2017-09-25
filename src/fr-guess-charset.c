#include <json-glib/json-glib.h>
#include "file-utils.h"
#include "fr-command-zip.h"
#include "fr-command-unarchiver.h"

typedef void (*DetectFunc)(JsonObject* root, void*);

static
void detect_by_lsar(const char* file, DetectFunc fn , void* data)
{
    if (!_g_program_is_in_path("lsar")) {
       return;
    }

    gchar* buf = 0;
    GError *err = 0;
    char* cmd  = g_strdup_printf("lsar -j %s", file);
    g_spawn_command_line_sync (cmd,	&buf, 0, 0, &err);
    g_free(cmd);

    if (err != 0) {
        g_warning("guess_encoding_by_lsar failed: %s\n", err->message);
        g_error_free(err);
        return;
    }

    JsonParser* parser = json_parser_new ();
    if (json_parser_load_from_data(parser, buf, -1, 0)) {
      JsonObject *root = json_node_get_object (json_parser_get_root (parser));
      fn(root, data);
    }
    g_free(buf);
}

static
void detect_encoding(JsonObject* root, void* data)
{
   char** v = data;
   *v = g_strdup(json_object_get_string_member (root, "lsarEncoding"));
}

char* guess_encoding_by_lsar(const char* file)
{
    char* encoding = 0;
    detect_by_lsar(file, detect_encoding, &encoding);
    return encoding;
}

static
void support_unzip(JsonObject* root, void* ret)
{
  gboolean *v = ret;
  const char* fmt = json_object_get_string_member (root, "lsarFormatName");
  if (0 != g_strcmp0(fmt, "Zip")) {
    *v = FALSE;
    return;
  }

  JsonArray* contents = json_object_get_array_member(root, "lsarContents");
  for (int i=0; i<json_array_get_length(contents); i++) {
    JsonNode* node = json_array_get_element(contents, i);
    JsonObject* obj = json_node_get_object(node);
    gint64 m = json_object_get_int_member(obj, "ZipCompressionMethod");
    if (m != 0 && m != 8) {
      *v = FALSE;
      g_warning("/usr/bin/unzip can't handle compression method of %ld\n", m);
      return;
    }
  }
  *v = TRUE;
}

static
void should_use_unar_for_tar(JsonObject* root, void *ret)
{
  gboolean *v = ret;
  const char* encoding = json_object_get_string_member (root, "lsarEncoding");
  if (0 == g_strcmp0(encoding, "gb18030") ||
      0 != g_strstr_len(encoding, 100, "2312")) {
    const char* fmt = json_object_get_string_member (root, "lsarFormatName");
    if (0 == g_strcmp0(fmt, "Tar")) {
      *v = TRUE;
      return;
    }
  }
  *v = FALSE;
}

GType guess_archive_type_by_lsar(GType t, GFile *file)
{
  GType ret = t;

  char* filename = g_file_get_path(file);

  gboolean v = FALSE;
  detect_by_lsar(filename, support_unzip, &v);
  if (v)  {
    ret =  FR_TYPE_COMMAND_ZIP;
  } else {
    detect_by_lsar(filename, should_use_unar_for_tar, &v);
    if (v)  {
      ret =  FR_TYPE_COMMAND_UNARCHIVER;
    }
  }

  g_free(filename);
  return ret;
}
