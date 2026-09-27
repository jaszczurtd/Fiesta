#include "sc_cli_output.h"

#include <stdio.h>

#include "sc_gps.h"
#include "sc_tests.h"

#include <stdarg.h>

const char *sc_cli_value_or_dash(const char *value) {
  return (value != 0 && value[0] != '\0') ? value : "-";
}

void sc_cli_typed_value_to_text(const ScTypedValue *value, char *buffer,
                                size_t buffer_size) {
  if (buffer == 0 || buffer_size == 0u) {
    return;
  }

  if (value == 0) {
    (void)snprintf(buffer, buffer_size, "-");
    return;
  }

  switch (value->type) {
  case SC_VALUE_TYPE_BOOL:
    (void)snprintf(buffer, buffer_size, "%s",
                   value->bool_value ? "true" : "false");
    return;
  case SC_VALUE_TYPE_INT:
    (void)snprintf(buffer, buffer_size, "%lld", (long long)value->int_value);
    return;
  case SC_VALUE_TYPE_UINT:
    (void)snprintf(buffer, buffer_size, "%llu",
                   (unsigned long long)value->uint_value);
    return;
  case SC_VALUE_TYPE_FLOAT:
    (void)snprintf(buffer, buffer_size, "%.6g", value->float_value);
    return;
  case SC_VALUE_TYPE_TEXT:
  case SC_VALUE_TYPE_UNKNOWN:
  default:
    (void)snprintf(buffer, buffer_size, "%s", sc_cli_value_or_dash(value->raw));
    return;
  }
}

void sc_cli_print_module_table(const ScCore *core) {
  if (core == 0) {
    return;
  }

  printf("%-12s %-8s %-9s %-10s %-22s %-14s %-10s %-10s\n", "MODULE", "FOUND",
         "INSTANCES", "AMBIGUOUS", "PORT", "UID", "FW", "BUILD");

  for (size_t i = 0u; i < sc_core_module_count(); ++i) {
    const ScModuleStatus *status = sc_core_module_status(core, i);
    if (status == 0) {
      continue;
    }

    printf("%-12s %-8s %-9zu %-10s %-22s %-14s %-10s %-10s\n",
           status->display_name, status->detected ? "yes" : "no",
           status->detected_instances, status->target_ambiguous ? "yes" : "no",
           status->detected ? status->port_path : "-",
           status->detected ? sc_cli_value_or_dash(status->hello_identity.uid)
                            : "-",
           status->detected
               ? sc_cli_value_or_dash(status->hello_identity.fw_version)
               : "-",
           status->detected
               ? sc_cli_value_or_dash(status->hello_identity.build_id)
               : "-");
  }
}

void sc_cli_print_parsed_values(const ScParamValuesData *values) {
  if (values == 0) {
    return;
  }

  printf("PARSED count=%zu%s\n", values->count,
         values->truncated ? " (truncated)" : "");
  for (size_t i = 0u; i < values->count; ++i) {
    char typed[128];
    sc_cli_typed_value_to_text(&values->entries[i].value, typed, sizeof(typed));
    printf("- %s = %s (type=%s)\n", values->entries[i].id, typed,
           sc_value_type_name(values->entries[i].value.type));
  }
}

void sc_cli_print_parsed_param_list(const ScParamListData *list) {
  if (list == 0) {
    return;
  }

  printf("PARSED count=%zu%s\n", list->count,
         list->truncated ? " (truncated)" : "");
  for (size_t i = 0u; i < list->count; ++i) {
    printf("- %s\n", list->ids[i]);
  }
}

void sc_cli_print_parsed_param_detail(const ScParamDetailData *detail) {
  if (detail == 0) {
    return;
  }

  char value_text[128];
  char min_text[128];
  char max_text[128];
  char default_text[128];
  sc_cli_typed_value_to_text(&detail->value, value_text, sizeof(value_text));
  sc_cli_typed_value_to_text(&detail->min, min_text, sizeof(min_text));
  sc_cli_typed_value_to_text(&detail->max, max_text, sizeof(max_text));
  sc_cli_typed_value_to_text(&detail->default_value, default_text,
                             sizeof(default_text));

  printf("PARSED id=%s valid=%s\n", detail->id,
         detail->valid ? "true" : "false");
  printf("  value=%s (type=%s)\n", value_text,
         sc_value_type_name(detail->value.type));
  if (detail->has_min) {
    printf("  min=%s (type=%s)\n", min_text,
           sc_value_type_name(detail->min.type));
  }
  if (detail->has_max) {
    printf("  max=%s (type=%s)\n", max_text,
           sc_value_type_name(detail->max.type));
  }
  if (detail->has_default) {
    printf("  default=%s (type=%s)\n", default_text,
           sc_value_type_name(detail->default_value.type));
  }
}

void sc_cli_print_gps_snapshot(const struct ScGpsSnapshot *snapshot) {
  if (snapshot == 0) {
    return;
  }

  /* Six decimal places matches the wire's microdegree resolution
   * (1e-6 deg ≈ 11 cm at the equator). Speed gets one decimal
   * because the firmware ships speed_kmh_x10. Epoch=0 means
   * "no GPS time available yet" - render it as `-` so shell users
   * can tell it apart from genuine 1970-01-01 fixes (which we'd
   * never see in practice but stay distinguishable). */
  printf("PARSED available=%s\n", snapshot->available ? "true" : "false");
  if (snapshot->available) {
    printf("  lat=%.6f deg\n", snapshot->latitude_deg);
    printf("  lon=%.6f deg\n", snapshot->longitude_deg);
    printf("  speed=%.1f km/h\n", snapshot->speed_kmh);
    if (snapshot->epoch_utc != 0u) {
      printf("  epoch=%lu (UTC seconds)\n", (unsigned long)snapshot->epoch_utc);
    } else {
      printf("  epoch=- (no GPS time yet)\n");
    }
  }
}

void sc_cli_print_test_catalog(const ScTestCatalog *catalog) {
  if (catalog == 0) {
    return;
  }
  if (!catalog->supported || catalog->count == 0u) {
    printf("No functional tests available.\n");
    return;
  }
  printf("%-10s %-9s %-16s %8s %8s %8s %8s  %s\n", "TEST", "SEQUENCE", "PARAM",
         "VALUE", "MIN", "MAX", "DEFAULT", "UNIT");
  for (size_t t = 0u; t < catalog->count; ++t) {
    const ScTestEntry *entry = &catalog->tests[t];
    const char *sequence = entry->in_sequence ? "yes" : "no";
    if (entry->param_count == 0u) {
      printf("%-10s %-9s %-16s\n", entry->name, sequence, "-");
    }
    for (size_t p = 0u; p < entry->param_count; ++p) {
      const ScTestParam *param = &entry->params[p];
      printf("%-10s %-9s %-16s %8ld %8ld %8ld %8ld  %s\n",
             (p == 0u) ? entry->name : "", (p == 0u) ? sequence : "", param->id,
             (long)param->value, (long)param->min, (long)param->max,
             (long)param->default_value, param->unit);
    }
  }
}

/* Append to a line buffer; stops quietly once it is full. */
static void line_append(char *buffer, size_t size, size_t *length,
                        const char *format, ...) {
  if (*length >= size) {
    return;
  }
  va_list args;
  va_start(args, format);
  const int written = vsnprintf(&buffer[*length], size - *length, format, args);
  va_end(args);
  if (written > 0) {
    *length += (size_t)written;
  }
}

void sc_cli_format_test_status(const ScTestStatus *status, bool with_time,
                               char *buffer, size_t buffer_size) {
  if (buffer == 0 || buffer_size == 0u) {
    return;
  }
  buffer[0] = '\0';
  if (status == 0) {
    return;
  }
  size_t length = 0u;
  if (!status->running) {
    line_append(buffer, buffer_size, &length, "IDLE runs=%lu",
                (unsigned long)status->runs);
  } else {
    line_append(buffer, buffer_size, &length, "RUNNING %s src=%s", status->test,
                sc_cli_value_or_dash(status->source));
    if (status->seq_count != 0u) {
      line_append(buffer, buffer_size, &length, " seq=%u/%u", status->seq_index,
                  status->seq_count);
    }
    if (with_time) {
      line_append(buffer, buffer_size, &length, " t=%lu.%lus",
                  (unsigned long)(status->elapsed_ms / 1000u),
                  (unsigned long)((status->elapsed_ms % 1000u) / 100u));
    }
    if (status->drive_valid) {
      line_append(buffer, buffer_size, &length,
                  " demand=%.1f%% position=%.1f%%",
                  (double)status->demand_x10 / 10.0,
                  (double)status->position_x10 / 10.0);
    }
    for (size_t i = 0u; i < status->field_count; ++i) {
      const ScTestField *field = &status->fields[i];
      if (field->is_text) {
        line_append(buffer, buffer_size, &length, " %s=%s", field->key,
                    field->text);
      } else {
        line_append(buffer, buffer_size, &length, " %s=%ld", field->key,
                    (long)field->value);
      }
    }
  }
  if (status->has_last) {
    line_append(buffer, buffer_size, &length, " last=%s result=%s",
                status->last, sc_cli_value_or_dash(status->result));
  }
}
