#include "pie/core/registry/registry.h"

const PieFeatureRegistry *pie_feature_registry_generated(void);

const PieFeatureRegistry *pie_feature_registry(void) {
  return pie_feature_registry_generated();
}

void pie_feature_registry_print(FILE *out, const PieFeatureRegistry *registry) {
  fprintf(out, "Pie feature registry: %zu feature(s)\n", registry->count);
  for (size_t i = 0; i < registry->count; i++) {
    const PieFeature *feature = &registry->features[i];
    fprintf(out, "  %-28s %-12s %s\n", feature->id, feature->version,
            feature->name);
  }
}
