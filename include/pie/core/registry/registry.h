#ifndef PIE_CORE_REGISTRY_REGISTRY_H
#define PIE_CORE_REGISTRY_REGISTRY_H

#include <stddef.h>
#include <stdio.h>

typedef struct PieFeature {
  const char *id;
  const char *name;
  const char *group;
  const char *version;
  const char *kind;
  const char *const *deps;
  size_t dep_count;
  const char *const *tokens;
  size_t token_count;
  const char *const *keywords;
  size_t keyword_count;
  const char *const *operators;
  size_t operator_count;
  const char *const *ast_nodes;
  size_t ast_node_count;
  const char *const *type_kinds;
  size_t type_kind_count;
  const char *const *hooks;
  size_t hook_count;
} PieFeature;

typedef struct PieFeatureRegistry {
  const PieFeature *features;
  size_t count;
} PieFeatureRegistry;

const PieFeatureRegistry *pie_feature_registry(void);
void pie_feature_registry_print(FILE *out, const PieFeatureRegistry *registry);

#endif
