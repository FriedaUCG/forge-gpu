#include "forge_gpu_processed_assets.h"

#include "forge_gpu_math.h"
#include "third_party/cJSON/cJSON.h"

#include <float.h>
#include <stddef.h>

#define FORGE_GPU_PROCESSED_FMESH_MAGIC "FMSH"
#define FORGE_GPU_PROCESSED_FMESH_VERSION 2u
#define FORGE_GPU_PROCESSED_FMESH_VERSION_SKINNED 3u
#define FORGE_GPU_PROCESSED_FMESH_HEADER_SIZE 32u
#define FORGE_GPU_PROCESSED_FMESH_MAX_LODS 8u
#define FORGE_GPU_PROCESSED_FMESH_MAX_SUBMESHES 64u
#define FORGE_GPU_PROCESSED_FSKIN_MAGIC "FSKN"
#define FORGE_GPU_PROCESSED_FSKIN_VERSION 1u
#define FORGE_GPU_PROCESSED_FSKIN_HEADER_SIZE 12u
#define FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE 64u
#define FORGE_GPU_PROCESSED_FSKIN_MAX_SKINS 64u
#define FORGE_GPU_PROCESSED_FSKIN_MAX_JOINTS 256u
#define FORGE_GPU_PROCESSED_FANIM_MAGIC "FANM"
#define FORGE_GPU_PROCESSED_FANIM_VERSION 1u
#define FORGE_GPU_PROCESSED_FANIM_HEADER_SIZE 12u
#define FORGE_GPU_PROCESSED_FANIM_NAME_SIZE 64u
#define FORGE_GPU_PROCESSED_FANIM_MAX_CLIPS 256u
#define FORGE_GPU_PROCESSED_FANIM_MAX_SAMPLERS 1024u
#define FORGE_GPU_PROCESSED_FANIM_MAX_CHANNELS 4096u
#define FORGE_GPU_PROCESSED_FANIM_MAX_KEYFRAMES 1048576u
#define FORGE_GPU_PROCESSED_FANIM_MAX_VALUE_COMPONENTS FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS
#define FORGE_GPU_PROCESSED_FSCENE_MAGIC "FSCN"
#define FORGE_GPU_PROCESSED_FSCENE_VERSION 1u
#define FORGE_GPU_PROCESSED_FSCENE_HEADER_SIZE 24u
#define FORGE_GPU_PROCESSED_FSCENE_NODE_SIZE 192u
#define FORGE_GPU_PROCESSED_FSCENE_MAX_NODES 4096u
#define FORGE_GPU_PROCESSED_FSCENE_MAX_ROOTS 256u
#define FORGE_GPU_PROCESSED_FSCENE_MAX_MESHES 1024u
#define FORGE_GPU_PROCESSED_FTEX_MAGIC 0x58455446u
#define FORGE_GPU_PROCESSED_FTEX_VERSION 1u
#define FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE 32u
#define FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE 16u
#define FORGE_GPU_PROCESSED_FTEX_MAX_MIP_LEVELS 32u
#define FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK 16u
#define FORGE_GPU_PROCESSED_FMAT_VERSION 1
#define FORGE_GPU_PROCESSED_MAX_MATERIALS 256u
#define FORGE_GPU_PROCESSED_MORPH_HEADER_SIZE 8u
#define FORGE_GPU_PROCESSED_MORPH_META_SIZE (FORGE_GPU_PROCESSED_MORPH_NAME_SIZE + 4u)

static Uint32 read_u32_le(const Uint8 *data)
{
    Uint32 value;

    SDL_memcpy(&value, data, sizeof(value));
    return SDL_Swap32LE(value);
}

static Sint32 read_i32_le(const Uint8 *data)
{
    Uint32 bits = read_u32_le(data);
    Sint32 value;

    SDL_memcpy(&value, &bits, sizeof(value));
    return value;
}

static float read_f32_le(const Uint8 *data)
{
    Uint32 bits = read_u32_le(data);
    float value;

    SDL_memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool checked_mul_size(size_t a, size_t b, size_t *out)
{
    if (b != 0 && a > ((size_t)-1) / b) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool checked_add_size(size_t a, size_t b, size_t *out)
{
    if (a > ((size_t)-1) - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static void free_processed_morph_targets(ForgeGpuProcessedMorphTarget *targets, Uint32 target_count)
{
    if (!targets) {
        return;
    }
    for (Uint32 target_index = 0; target_index < target_count; target_index += 1) {
        SDL_free(targets[target_index].position_deltas);
        SDL_free(targets[target_index].normal_deltas);
        SDL_free(targets[target_index].tangent_deltas);
    }
    SDL_free(targets);
}

static bool validate_relative_asset_path(const char *field_name, const char *path)
{
    const char *component_start;

    if (!path || path[0] == '\0') {
        return true;
    }
    if (path[0] == '/' || path[0] == '\\' || SDL_strchr(path, ':')) {
        SDL_SetError("forge processed assets: '%s' must be a relative asset path", field_name);
        return false;
    }

    component_start = path;
    for (const char *p = path;; p += 1) {
        if (*p == '/' || *p == '\\' || *p == '\0') {
            const size_t len = (size_t)(p - component_start);

            if (len == 0 ||
                (len == 1 && component_start[0] == '.') ||
                (len == 2 && component_start[0] == '.' && component_start[1] == '.')) {
                SDL_SetError("forge processed assets: '%s' must not contain traversal components", field_name);
                return false;
            }
            if (*p == '\0') {
                break;
            }
            component_start = p + 1;
        }
    }
    return true;
}

static void set_processed_file_error(const char *path)
{
    const char *error = SDL_GetError();
    char *error_copy = error && error[0] ? SDL_strdup(error) : NULL;

    SDL_SetError(
        "forge processed assets: failed to load '%s': %s",
        path ? path : "(null)",
        error_copy ? error_copy : "unknown error");
    SDL_free(error_copy);
}

static char *load_text_file(const char *path, size_t *out_size)
{
    size_t size = 0;
    char *data = (char *)SDL_LoadFile(path, &size);

    if (!data) {
        set_processed_file_error(path);
        return NULL;
    }
    if (out_size) {
        *out_size = size;
    }
    return data;
}

static cJSON *load_json_file(const char *path)
{
    size_t size = 0;
    char *data = load_text_file(path, &size);
    const char *parse_end = NULL;
    const char *file_end;
    cJSON *json;
    bool json_error_set = false;

    if (!data) {
        return NULL;
    }
    json = cJSON_ParseWithLengthOpts(data, size, &parse_end, false);
    file_end = data + size;
    if (json) {
        while (parse_end < file_end && SDL_isspace((unsigned char)*parse_end)) {
            parse_end += 1;
        }
        if (parse_end != file_end) {
            cJSON_Delete(json);
            json = NULL;
            json_error_set = true;
            SDL_SetError("forge processed assets: JSON '%s' has trailing non-whitespace data", path);
        }
    }
    SDL_free(data);
    if (!json) {
        if (!json_error_set) {
            SDL_SetError("forge processed assets: failed to parse JSON '%s'", path);
        }
        return NULL;
    }
    return json;
}

static bool copy_json_string(
    const cJSON *object,
    const char *name,
    char *dst,
    size_t dst_size)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!value) {
        if (dst_size > 0) {
            dst[0] = '\0';
        }
        return true;
    }
    if (cJSON_IsNull(value)) {
        if (dst_size > 0) {
            dst[0] = '\0';
        }
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring) {
        SDL_SetError("forge processed assets: '%s' must be a string", name);
        return false;
    }
    if (SDL_strlen(value->valuestring) >= dst_size) {
        SDL_SetError("forge processed assets: '%s' is too long", name);
        return false;
    }
    SDL_strlcpy(dst, value->valuestring, dst_size);
    return true;
}

static bool copy_json_asset_path(
    const cJSON *object,
    const char *name,
    char *dst,
    size_t dst_size)
{
    if (!copy_json_string(object, name, dst, dst_size)) {
        return false;
    }
    return validate_relative_asset_path(name, dst);
}

static bool read_json_float_array3(
    const cJSON *object,
    const char *name,
    float fallback[3],
    float dst[3])
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!array) {
        SDL_memcpy(dst, fallback, sizeof(float) * 3);
        return true;
    }
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 3) {
        SDL_SetError("forge processed assets: '%s' must be a three-number array", name);
        return false;
    }
    for (int i = 0; i < 3; i += 1) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsNumber(item)) {
            SDL_SetError("forge processed assets: '%s' element %d must be numeric", name, i);
            return false;
        }
        dst[i] = (float)item->valuedouble;
    }
    return true;
}

static bool read_json_float_array4(
    const cJSON *object,
    const char *name,
    float fallback[4],
    float dst[4])
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!array) {
        SDL_memcpy(dst, fallback, sizeof(float) * 4);
        return true;
    }
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 4) {
        SDL_SetError("forge processed assets: '%s' must be a four-number array", name);
        return false;
    }
    for (int i = 0; i < 4; i += 1) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsNumber(item)) {
            SDL_SetError("forge processed assets: '%s' element %d must be numeric", name, i);
            return false;
        }
        dst[i] = (float)item->valuedouble;
    }
    return true;
}

static bool read_json_optional_float(
    const cJSON *object,
    const char *name,
    float fallback,
    float *dst)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!value) {
        *dst = fallback;
        return true;
    }
    if (!cJSON_IsNumber(value)) {
        SDL_SetError("forge processed assets: '%s' must be numeric", name);
        return false;
    }
    *dst = (float)value->valuedouble;
    return true;
}

static void mat4_identity(float m[16])
{
    SDL_memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void mat4_multiply(float out[16], const float a[16], const float b[16])
{
    float result[16];

    for (int col = 0; col < 4; col += 1) {
        for (int row = 0; row < 4; row += 1) {
            float sum = 0.0f;

            for (int k = 0; k < 4; k += 1) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }
    SDL_memcpy(out, result, sizeof(result));
}

static char *meta_path_for_image(const char *image_path)
{
    const char *dot = SDL_strrchr(image_path, '.');
    const size_t stem_len = dot ? (size_t)(dot - image_path) : SDL_strlen(image_path);
    const char *suffix = ".meta.json";
    const size_t suffix_len = SDL_strlen(suffix);
    char *path = (char *)SDL_malloc(stem_len + suffix_len + 1);

    if (!path) {
        return NULL;
    }
    SDL_memcpy(path, image_path, stem_len);
    SDL_memcpy(path + stem_len, suffix, suffix_len);
    path[stem_len + suffix_len] = '\0';
    return path;
}

static bool read_json_u64(
    const cJSON *object,
    const char *name,
    bool required,
    Uint64 *dst)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!value) {
        if (required) {
            SDL_SetError("forge processed assets: '%s' is missing", name);
            return false;
        }
        *dst = 0;
        return true;
    }
    if (!cJSON_IsNumber(value) || value->valuedouble < 0.0 || value->valuedouble > (double)SDL_MAX_UINT64) {
        SDL_SetError("forge processed assets: '%s' must be a non-negative integer", name);
        return false;
    }
    *dst = (Uint64)value->valuedouble;
    if ((double)*dst != value->valuedouble) {
        SDL_SetError("forge processed assets: '%s' must be an integer", name);
        return false;
    }
    return true;
}

static bool read_json_u32(
    const cJSON *object,
    const char *name,
    bool required,
    Uint32 *dst)
{
    Uint64 value;

    if (!read_json_u64(object, name, required, &value)) {
        return false;
    }
    if (value > SDL_MAX_UINT32) {
        SDL_SetError("forge processed assets: '%s' is too large", name);
        return false;
    }
    *dst = (Uint32)value;
    return true;
}

static bool processed_ftex_format_from_string(const char *format, Uint32 *out_format)
{
    if (!format || !out_format) {
        SDL_SetError("forge processed assets: missing .ftex format");
        return false;
    }
    if (SDL_strcmp(format, "bc7_srgb") == 0) {
        *out_format = FORGE_GPU_PROCESSED_FTEX_BC7_SRGB;
        return true;
    }
    if (SDL_strcmp(format, "bc7_unorm") == 0) {
        *out_format = FORGE_GPU_PROCESSED_FTEX_BC7_UNORM;
        return true;
    }
    if (SDL_strcmp(format, "bc5_unorm") == 0) {
        *out_format = FORGE_GPU_PROCESSED_FTEX_BC5_UNORM;
        return true;
    }
    SDL_SetError("forge processed assets: unsupported .ftex format '%s'", format);
    return false;
}

const char *ForgeGpuProcessedBasename(const char *path)
{
    const char *slash;
    const char *backslash;

    if (!path) {
        return "";
    }
    slash = SDL_strrchr(path, '/');
    backslash = SDL_strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

bool ForgeGpuProcessedMeshHasTangents(const ForgeGpuProcessedMesh *mesh)
{
    return mesh &&
           (mesh->flags & FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS) != 0 &&
           (mesh->vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS ||
            mesh->vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS);
}

bool ForgeGpuProcessedMeshIsSkinned(const ForgeGpuProcessedMesh *mesh)
{
    return mesh &&
           (mesh->flags & FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED) != 0 &&
           (mesh->vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED ||
            mesh->vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS);
}

bool ForgeGpuProcessedMeshHasMorphs(const ForgeGpuProcessedMesh *mesh)
{
    return mesh &&
           (mesh->flags & FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS) != 0 &&
           mesh->morph_targets &&
           mesh->morph_target_count > 0 &&
           (mesh->morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION) != 0;
}

Uint32 ForgeGpuProcessedMeshLodIndexCount(const ForgeGpuProcessedMesh *mesh, Uint32 lod)
{
    if (!mesh || lod >= mesh->lod_count) {
        return 0;
    }
    return mesh->lods[lod].index_count;
}

Uint32 ForgeGpuProcessedMeshLodFirstIndex(const ForgeGpuProcessedMesh *mesh, Uint32 lod)
{
    if (!mesh || lod >= mesh->lod_count) {
        return 0;
    }
    return mesh->lods[lod].index_offset / (Uint32)sizeof(Uint32);
}

void ForgeGpuFreeProcessedMesh(ForgeGpuProcessedMesh *mesh)
{
    if (!mesh) {
        return;
    }
    SDL_free(mesh->vertices);
    SDL_free(mesh->indices);
    SDL_free(mesh->lods);
    SDL_free(mesh->submeshes);
    free_processed_morph_targets(mesh->morph_targets, mesh->morph_target_count);
    SDL_zero(*mesh);
}

static bool validate_lod_submesh_spans(
    const char *path,
    const ForgeGpuProcessedSubmesh *submeshes,
    Uint32 lod_count,
    Uint32 submesh_count)
{
    ForgeGpuProcessedSubmesh *sort_tmp = (ForgeGpuProcessedSubmesh *)SDL_malloc(
        sizeof(*sort_tmp) * submesh_count);

    if (!sort_tmp) {
        SDL_SetError("forge processed assets: mesh '%s' span validation allocation failed", path);
        return false;
    }

    for (Uint32 lod = 0; lod < lod_count; lod += 1) {
        SDL_memcpy(
            sort_tmp,
            &submeshes[(size_t)lod * submesh_count],
            sizeof(*sort_tmp) * submesh_count);

        for (Uint32 i = 0; i < submesh_count; i += 1) {
            for (Uint32 j = i + 1; j < submesh_count; j += 1) {
                if (sort_tmp[j].index_offset < sort_tmp[i].index_offset) {
                    ForgeGpuProcessedSubmesh swap = sort_tmp[i];
                    sort_tmp[i] = sort_tmp[j];
                    sort_tmp[j] = swap;
                }
            }
        }

        for (Uint32 submesh = 0; submesh + 1 < submesh_count; submesh += 1) {
            const Uint64 current_end =
                (Uint64)sort_tmp[submesh].index_offset +
                (Uint64)sort_tmp[submesh].index_count * sizeof(Uint32);

            if (current_end != (Uint64)sort_tmp[submesh + 1].index_offset) {
                SDL_SetError("forge processed assets: mesh '%s' LOD %u submesh spans are not contiguous", path, lod);
                SDL_free(sort_tmp);
                return false;
            }
        }
    }

    SDL_free(sort_tmp);
    return true;
}

bool ForgeGpuLoadProcessedMesh(const char *path, ForgeGpuProcessedMesh *mesh)
{
    size_t file_size = 0;
    Uint8 *file_data;
    const Uint8 *p;
    const Uint8 *end;
    Uint32 version;
    Uint32 vertex_count;
    Uint32 vertex_stride;
    Uint32 lod_count;
    Uint32 flags;
    Uint32 submesh_count;
    size_t per_lod_size;
    size_t lod_table_size;
    size_t vertex_size;
    size_t index_size;
    size_t required_size;
    Uint64 total_indices64 = 0;
    ForgeGpuProcessedLod *lods = NULL;
    ForgeGpuProcessedSubmesh *submeshes = NULL;
    ForgeGpuProcessedMorphTarget *morph_targets = NULL;
    Uint32 morph_target_count = 0;
    Uint32 morph_attribute_flags = 0;
    void *vertices = NULL;
    Uint32 *indices = NULL;

    if (!path || !mesh) {
        SDL_SetError("forge processed assets: mesh path or output storage is missing");
        return false;
    }

    SDL_zero(*mesh);
    file_data = (Uint8 *)SDL_LoadFile(path, &file_size);
    if (!file_data) {
        set_processed_file_error(path);
        return false;
    }
    if (file_size < FORGE_GPU_PROCESSED_FMESH_HEADER_SIZE) {
        SDL_SetError("forge processed assets: mesh '%s' is too small", path);
        goto fail;
    }

    p = file_data;
    end = file_data + file_size;
    if (SDL_memcmp(p, FORGE_GPU_PROCESSED_FMESH_MAGIC, 4) != 0) {
        SDL_SetError("forge processed assets: mesh '%s' has bad magic", path);
        goto fail;
    }
    p += 4;
    version = read_u32_le(p);
    p += 4;
    if (version != FORGE_GPU_PROCESSED_FMESH_VERSION &&
        version != FORGE_GPU_PROCESSED_FMESH_VERSION_SKINNED) {
        SDL_SetError("forge processed assets: mesh '%s' uses unsupported version %u", path, version);
        goto fail;
    }

    vertex_count = read_u32_le(p);
    p += 4;
    vertex_stride = read_u32_le(p);
    p += 4;
    lod_count = read_u32_le(p);
    p += 4;
    flags = read_u32_le(p);
    p += 4;
    submesh_count = read_u32_le(p);
    p += 4;
    p += 4; /* reserved */

    if (vertex_count == 0 ||
        (vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_NO_TANGENTS &&
         vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS &&
         vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED &&
         vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS) ||
        lod_count == 0 || lod_count > FORGE_GPU_PROCESSED_FMESH_MAX_LODS ||
        submesh_count == 0 || submesh_count > FORGE_GPU_PROCESSED_FMESH_MAX_SUBMESHES ||
        (flags & ~(FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS |
                   FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED |
                   FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS)) != 0) {
        SDL_SetError("forge processed assets: mesh '%s' header is invalid", path);
        goto fail;
    }
    if (((flags & FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS) != 0) !=
        (vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS ||
         vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS)) {
        SDL_SetError("forge processed assets: mesh '%s' tangent flag/stride mismatch", path);
        goto fail;
    }
    if (((flags & FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED) != 0) !=
        (vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED ||
         vertex_stride == FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS)) {
        SDL_SetError("forge processed assets: mesh '%s' skin flag/stride mismatch", path);
        goto fail;
    }
    if ((flags & FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED) != 0 &&
        version != FORGE_GPU_PROCESSED_FMESH_VERSION_SKINNED) {
        SDL_SetError("forge processed assets: mesh '%s' uses skinned data before .fmesh v3", path);
        goto fail;
    }
    if ((flags & FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS) != 0 &&
        version != FORGE_GPU_PROCESSED_FMESH_VERSION_SKINNED) {
        SDL_SetError("forge processed assets: mesh '%s' uses morph target data before .fmesh v3", path);
        goto fail;
    }

    if (!checked_mul_size(submesh_count, 12u, &per_lod_size) ||
        !checked_add_size(per_lod_size, 4u, &per_lod_size) ||
        !checked_mul_size(lod_count, per_lod_size, &lod_table_size) ||
        !checked_mul_size(vertex_count, vertex_stride, &vertex_size)) {
        SDL_SetError("forge processed assets: mesh '%s' size arithmetic overflow", path);
        goto fail;
    }
    if (!checked_add_size(FORGE_GPU_PROCESSED_FMESH_HEADER_SIZE, lod_table_size, &required_size) ||
        file_size < required_size) {
        SDL_SetError("forge processed assets: mesh '%s' is truncated before vertex data", path);
        goto fail;
    }

    lods = (ForgeGpuProcessedLod *)SDL_calloc(lod_count, sizeof(*lods));
    submeshes = (ForgeGpuProcessedSubmesh *)SDL_calloc((size_t)lod_count * submesh_count, sizeof(*submeshes));
    if (!lods || !submeshes) {
        SDL_SetError("forge processed assets: mesh '%s' allocation failed", path);
        goto fail;
    }

    for (Uint32 lod = 0; lod < lod_count; lod += 1) {
        Uint64 lod_index_count = 0;
        Uint32 lod_index_offset = SDL_MAX_UINT32;

        lods[lod].target_error = read_f32_le(p);
        p += 4;
        for (Uint32 submesh = 0; submesh < submesh_count; submesh += 1) {
            const size_t idx = (size_t)lod * submesh_count + submesh;
            Uint32 mat_bits;

            submeshes[idx].index_count = read_u32_le(p);
            p += 4;
            submeshes[idx].index_offset = read_u32_le(p);
            p += 4;
            mat_bits = read_u32_le(p);
            p += 4;
            SDL_memcpy(&submeshes[idx].material_index, &mat_bits, sizeof(submeshes[idx].material_index));

            if ((submeshes[idx].index_offset % sizeof(Uint32)) != 0) {
                SDL_SetError("forge processed assets: mesh '%s' has misaligned index offset", path);
                goto fail;
            }
            lod_index_count += submeshes[idx].index_count;
            total_indices64 += submeshes[idx].index_count;
            if (submeshes[idx].index_offset < lod_index_offset) {
                lod_index_offset = submeshes[idx].index_offset;
            }
        }
        if (lod_index_count > SDL_MAX_UINT32 || total_indices64 > SDL_MAX_UINT32) {
            SDL_SetError("forge processed assets: mesh '%s' has too many indices", path);
            goto fail;
        }
        lods[lod].index_count = (Uint32)lod_index_count;
        lods[lod].index_offset = lod_index_offset == SDL_MAX_UINT32 ? 0 : lod_index_offset;
    }

    for (Uint32 lod = 0; lod < lod_count; lod += 1) {
        for (Uint32 submesh = 0; submesh < submesh_count; submesh += 1) {
            const size_t idx = (size_t)lod * submesh_count + submesh;
            const Uint64 first_index = (Uint64)submeshes[idx].index_offset / sizeof(Uint32);
            const Uint64 range_end = first_index + submeshes[idx].index_count;

            if (range_end > total_indices64) {
                SDL_SetError("forge processed assets: mesh '%s' submesh index range is invalid", path);
                goto fail;
            }
        }
    }

    if (!validate_lod_submesh_spans(path, submeshes, lod_count, submesh_count)) {
        goto fail;
    }

    if (!checked_mul_size((size_t)total_indices64, sizeof(Uint32), &index_size) ||
        !checked_add_size(FORGE_GPU_PROCESSED_FMESH_HEADER_SIZE, lod_table_size, &required_size) ||
        !checked_add_size(required_size, vertex_size, &required_size) ||
        !checked_add_size(required_size, index_size, &required_size) ||
        file_size < required_size) {
        SDL_SetError("forge processed assets: mesh '%s' is truncated", path);
        goto fail;
    }

    vertices = SDL_malloc(vertex_size);
    indices = (Uint32 *)SDL_malloc(index_size);
    if (!vertices || !indices) {
        SDL_SetError("forge processed assets: mesh '%s' vertex/index allocation failed", path);
        goto fail;
    }
    SDL_memcpy(vertices, p, vertex_size);
    p += vertex_size;
    SDL_memcpy(indices, p, index_size);
    p += index_size;

    for (Uint32 i = 0; i < (Uint32)total_indices64; i += 1) {
        if (indices[i] >= vertex_count) {
            SDL_SetError("forge processed assets: mesh '%s' index %u is out of bounds", path, i);
            goto fail;
        }
    }

    if ((flags & FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS) != 0) {
        const Uint32 valid_morph_flags =
            FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION |
            FORGE_GPU_PROCESSED_MORPH_ATTR_NORMAL |
            FORGE_GPU_PROCESSED_MORPH_ATTR_TANGENT;
        Uint32 attrs_per_target = 0;
        size_t meta_size;
        size_t delta_floats_per_attr;
        size_t delta_bytes_per_attr;

        if ((size_t)(end - p) < FORGE_GPU_PROCESSED_MORPH_HEADER_SIZE) {
            SDL_SetError("forge processed assets: mesh '%s' is truncated at morph header", path);
            goto fail;
        }
        morph_target_count = read_u32_le(p);
        p += 4;
        morph_attribute_flags = read_u32_le(p);
        p += 4;

        if (morph_target_count == 0 ||
            morph_target_count > FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS ||
            (morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION) == 0 ||
            (morph_attribute_flags & ~valid_morph_flags) != 0) {
            SDL_SetError("forge processed assets: mesh '%s' morph header is invalid", path);
            goto fail;
        }

        if (!checked_mul_size(morph_target_count, FORGE_GPU_PROCESSED_MORPH_META_SIZE, &meta_size) ||
            (size_t)(end - p) < meta_size) {
            SDL_SetError("forge processed assets: mesh '%s' is truncated at morph metadata", path);
            goto fail;
        }

        morph_targets = (ForgeGpuProcessedMorphTarget *)SDL_calloc(morph_target_count, sizeof(*morph_targets));
        if (!morph_targets) {
            SDL_SetError("forge processed assets: mesh '%s' morph allocation failed", path);
            goto fail;
        }

        for (Uint32 target_index = 0; target_index < morph_target_count; target_index += 1) {
            SDL_memcpy(morph_targets[target_index].name, p, FORGE_GPU_PROCESSED_MORPH_NAME_SIZE);
            morph_targets[target_index].name[FORGE_GPU_PROCESSED_MORPH_NAME_SIZE - 1] = '\0';
            p += FORGE_GPU_PROCESSED_MORPH_NAME_SIZE;
            morph_targets[target_index].default_weight = read_f32_le(p);
            p += 4;
        }

        if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION) != 0) {
            attrs_per_target += 1;
        }
        if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_NORMAL) != 0) {
            attrs_per_target += 1;
        }
        if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_TANGENT) != 0) {
            attrs_per_target += 1;
        }
        if (!checked_mul_size(vertex_count, 3u, &delta_floats_per_attr) ||
            !checked_mul_size(delta_floats_per_attr, sizeof(float), &delta_bytes_per_attr)) {
            SDL_SetError("forge processed assets: mesh '%s' morph size arithmetic overflow", path);
            goto fail;
        }
        if (delta_bytes_per_attr != 0 &&
            attrs_per_target > ((size_t)-1) / delta_bytes_per_attr / morph_target_count) {
            SDL_SetError("forge processed assets: mesh '%s' morph size arithmetic overflow", path);
            goto fail;
        }
        if ((size_t)(end - p) <
            delta_bytes_per_attr * attrs_per_target * morph_target_count) {
            SDL_SetError("forge processed assets: mesh '%s' is truncated at morph deltas", path);
            goto fail;
        }

        for (Uint32 target_index = 0; target_index < morph_target_count; target_index += 1) {
            ForgeGpuProcessedMorphTarget *target = &morph_targets[target_index];

            if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION) != 0) {
                target->position_deltas = (float *)SDL_malloc(delta_bytes_per_attr);
                if (!target->position_deltas) {
                    SDL_SetError("forge processed assets: mesh '%s' morph delta allocation failed", path);
                    goto fail;
                }
                for (size_t i = 0; i < delta_floats_per_attr; i += 1) {
                    target->position_deltas[i] = read_f32_le(p);
                    p += 4;
                }
            }
            if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_NORMAL) != 0) {
                target->normal_deltas = (float *)SDL_malloc(delta_bytes_per_attr);
                if (!target->normal_deltas) {
                    SDL_SetError("forge processed assets: mesh '%s' morph delta allocation failed", path);
                    goto fail;
                }
                for (size_t i = 0; i < delta_floats_per_attr; i += 1) {
                    target->normal_deltas[i] = read_f32_le(p);
                    p += 4;
                }
            }
            if ((morph_attribute_flags & FORGE_GPU_PROCESSED_MORPH_ATTR_TANGENT) != 0) {
                target->tangent_deltas = (float *)SDL_malloc(delta_bytes_per_attr);
                if (!target->tangent_deltas) {
                    SDL_SetError("forge processed assets: mesh '%s' morph delta allocation failed", path);
                    goto fail;
                }
                for (size_t i = 0; i < delta_floats_per_attr; i += 1) {
                    target->tangent_deltas[i] = read_f32_le(p);
                    p += 4;
                }
            }
        }
    }

    if (p != end) {
        SDL_SetError("forge processed assets: mesh '%s' has trailing data", path);
        goto fail;
    }

    mesh->vertices = vertices;
    mesh->indices = indices;
    mesh->vertex_count = vertex_count;
    mesh->vertex_stride = vertex_stride;
    mesh->lods = lods;
    mesh->lod_count = lod_count;
    mesh->flags = flags;
    mesh->submeshes = submeshes;
    mesh->submesh_count = submesh_count;
    mesh->total_index_count = (Uint32)total_indices64;
    mesh->morph_targets = morph_targets;
    mesh->morph_target_count = morph_target_count;
    mesh->morph_attribute_flags = morph_attribute_flags;
    SDL_free(file_data);
    return true;

fail:
    free_processed_morph_targets(morph_targets, morph_target_count);
    SDL_free(indices);
    SDL_free(vertices);
    SDL_free(submeshes);
    SDL_free(lods);
    SDL_free(file_data);
    SDL_zero(*mesh);
    return false;
}

void ForgeGpuFreeProcessedMaterials(ForgeGpuProcessedMaterialSet *set)
{
    if (!set) {
        return;
    }
    SDL_free(set->materials);
    SDL_zero(*set);
}

bool ForgeGpuLoadProcessedMaterials(const char *path, ForgeGpuProcessedMaterialSet *set)
{
    cJSON *root;
    const cJSON *version;
    const cJSON *materials_json;
    int material_count;
    ForgeGpuProcessedMaterial *materials = NULL;
    float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float black[3] = { 0.0f, 0.0f, 0.0f };

    if (!path || !set) {
        SDL_SetError("forge processed assets: material path or output storage is missing");
        return false;
    }
    SDL_zero(*set);

    root = load_json_file(path);
    if (!root) {
        return false;
    }

    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    materials_json = cJSON_GetObjectItemCaseSensitive(root, "materials");
    if (!cJSON_IsNumber(version) || version->valueint != FORGE_GPU_PROCESSED_FMAT_VERSION ||
        !cJSON_IsArray(materials_json)) {
        SDL_SetError("forge processed assets: material set '%s' has invalid header", path);
        goto fail;
    }
    material_count = cJSON_GetArraySize(materials_json);
    if (material_count < 0 || material_count > (int)FORGE_GPU_PROCESSED_MAX_MATERIALS) {
        SDL_SetError("forge processed assets: material set '%s' has invalid material count", path);
        goto fail;
    }
    if (material_count == 0) {
        set->materials = NULL;
        set->material_count = 0;
        cJSON_Delete(root);
        return true;
    }

    materials = (ForgeGpuProcessedMaterial *)SDL_calloc((size_t)material_count, sizeof(*materials));
    if (!materials) {
        SDL_SetError("forge processed assets: material allocation failed");
        goto fail;
    }

    for (int i = 0; i < material_count; i += 1) {
        const cJSON *item = cJSON_GetArrayItem(materials_json, i);
        ForgeGpuProcessedMaterial *material = &materials[i];

        if (!cJSON_IsObject(item)) {
            SDL_SetError("forge processed assets: material %d in '%s' is not an object", i, path);
            goto fail;
        }
        material->metallic_factor = 1.0f;
        material->roughness_factor = 1.0f;
        material->normal_scale = 1.0f;
        material->occlusion_strength = 1.0f;
        material->alpha_mode = FORGE_GPU_PROCESSED_ALPHA_OPAQUE;
        material->alpha_cutoff = 0.5f;
        if (!copy_json_string(item, "name", material->name, sizeof(material->name)) ||
            !read_json_float_array4(item, "base_color_factor", white, material->base_color_factor) ||
            !copy_json_asset_path(item, "base_color_texture", material->base_color_texture, sizeof(material->base_color_texture)) ||
            !read_json_optional_float(item, "metallic_factor", 1.0f, &material->metallic_factor) ||
            !read_json_optional_float(item, "roughness_factor", 1.0f, &material->roughness_factor) ||
            !copy_json_asset_path(item, "metallic_roughness_texture", material->metallic_roughness_texture, sizeof(material->metallic_roughness_texture)) ||
            !copy_json_asset_path(item, "roughness_texture", material->roughness_texture, sizeof(material->roughness_texture)) ||
            !copy_json_asset_path(item, "metallic_texture", material->metallic_texture, sizeof(material->metallic_texture)) ||
            !copy_json_asset_path(item, "normal_texture", material->normal_texture, sizeof(material->normal_texture)) ||
            !read_json_optional_float(item, "normal_scale", 1.0f, &material->normal_scale) ||
            !copy_json_asset_path(item, "occlusion_texture", material->occlusion_texture, sizeof(material->occlusion_texture)) ||
            !read_json_optional_float(item, "occlusion_strength", 1.0f, &material->occlusion_strength) ||
            !read_json_float_array3(item, "emissive_factor", black, material->emissive_factor) ||
            !copy_json_asset_path(item, "emissive_texture", material->emissive_texture, sizeof(material->emissive_texture))) {
            goto fail;
        }

        const cJSON *alpha_mode = cJSON_GetObjectItemCaseSensitive(item, "alpha_mode");
        if (cJSON_IsString(alpha_mode) && alpha_mode->valuestring) {
            if (SDL_strcmp(alpha_mode->valuestring, "MASK") == 0) {
                material->alpha_mode = FORGE_GPU_PROCESSED_ALPHA_MASK;
            } else if (SDL_strcmp(alpha_mode->valuestring, "BLEND") == 0) {
                material->alpha_mode = FORGE_GPU_PROCESSED_ALPHA_BLEND;
            } else if (SDL_strcmp(alpha_mode->valuestring, "OPAQUE") == 0) {
                material->alpha_mode = FORGE_GPU_PROCESSED_ALPHA_OPAQUE;
            } else {
                SDL_SetError("forge processed assets: material %d in '%s' has invalid alpha_mode", i, path);
                goto fail;
            }
        } else if (alpha_mode && !cJSON_IsNull(alpha_mode)) {
            SDL_SetError("forge processed assets: material %d in '%s' has invalid alpha_mode", i, path);
            goto fail;
        }

        if (!read_json_optional_float(item, "alpha_cutoff", 0.5f, &material->alpha_cutoff)) {
            goto fail;
        }

        const cJSON *double_sided = cJSON_GetObjectItemCaseSensitive(item, "double_sided");
        if (cJSON_IsBool(double_sided)) {
            material->double_sided = cJSON_IsTrue(double_sided);
        } else if (double_sided && !cJSON_IsNull(double_sided)) {
            SDL_SetError("forge processed assets: material %d in '%s' has invalid double_sided", i, path);
            goto fail;
        }
    }

    set->materials = materials;
    set->material_count = (Uint32)material_count;
    cJSON_Delete(root);
    return true;

fail:
    SDL_free(materials);
    cJSON_Delete(root);
    SDL_zero(*set);
    return false;
}

static bool compute_scene_world_transforms(
    ForgeGpuProcessedScene *scene,
    Uint32 node_index,
    Sint32 expected_parent,
    const float parent_world[16],
    Uint32 depth,
    Uint8 *visited)
{
    ForgeGpuProcessedSceneNode *node;

    if (node_index >= scene->node_count) {
        SDL_SetError("forge processed assets: scene node index %u is out of range", (unsigned int)node_index);
        return false;
    }
    if (depth >= scene->node_count) {
        SDL_SetError("forge processed assets: scene hierarchy contains a cycle at node %u", (unsigned int)node_index);
        return false;
    }
    if (visited[node_index]) {
        SDL_SetError("forge processed assets: scene node %u is shared or cyclic", (unsigned int)node_index);
        return false;
    }

    node = &scene->nodes[node_index];
    if (node->parent != expected_parent) {
        SDL_SetError(
            "forge processed assets: scene node %u has parent %d, expected %d",
            (unsigned int)node_index,
            (int)node->parent,
            (int)expected_parent);
        return false;
    }

    visited[node_index] = 1;
    mat4_multiply(node->world_transform, parent_world, node->local_transform);

    for (Uint32 i = 0; i < node->child_count; i += 1) {
        const Uint32 child_array_index = node->first_child + i;

        if (child_array_index >= scene->child_count) {
            SDL_SetError("forge processed assets: scene node %u child range is invalid", (unsigned int)node_index);
            return false;
        }
        if (!compute_scene_world_transforms(
                scene,
                scene->children[child_array_index],
                (Sint32)node_index,
                node->world_transform,
                depth + 1,
                visited)) {
            return false;
        }
    }
    return true;
}

void ForgeGpuFreeProcessedScene(ForgeGpuProcessedScene *scene)
{
    if (!scene) {
        return;
    }
    SDL_free(scene->nodes);
    SDL_free(scene->meshes);
    SDL_free(scene->roots);
    SDL_free(scene->children);
    SDL_zero(*scene);
}

bool ForgeGpuRecomputeProcessedSceneWorldTransforms(ForgeGpuProcessedScene *scene)
{
    float identity[16];
    Uint8 *visited = NULL;
    bool ok = true;

    if (!scene) {
        SDL_SetError("forge processed assets: invalid scene transform recompute arguments");
        return false;
    }
    if (scene->node_count == 0) {
        return true;
    }
    if (!scene->nodes || (scene->root_count > 0 && !scene->roots) ||
        (scene->child_count > 0 && !scene->children)) {
        SDL_SetError("forge processed assets: scene hierarchy is not retained");
        return false;
    }

    visited = (Uint8 *)SDL_calloc(scene->node_count, sizeof(*visited));
    if (!visited) {
        SDL_OutOfMemory();
        return false;
    }

    mat4_identity(identity);
    for (Uint32 i = 0; i < scene->root_count; i += 1) {
        if (!compute_scene_world_transforms(scene, scene->roots[i], -1, identity, 0, visited)) {
            ok = false;
            break;
        }
    }
    if (ok) {
        for (Uint32 i = 0; i < scene->node_count; i += 1) {
            if (!visited[i]) {
                SDL_SetError("forge processed assets: scene node %u is unreachable from roots", (unsigned)i);
                ok = false;
                break;
            }
        }
    }

    SDL_free(visited);
    return ok;
}

bool ForgeGpuValidateProcessedSceneModelReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedMesh *mesh,
    const ForgeGpuProcessedMaterialSet *materials,
    const char *label)
{
    const char *name = label && label[0] ? label : "processed scene";

    if (!scene || !mesh || !materials) {
        SDL_SetError("forge processed assets: %s reference validation arguments are missing", name);
        return false;
    }
    if ((scene->mesh_count > 0 && !scene->meshes) ||
        (mesh->lod_count > 0 && mesh->submesh_count > 0 && !mesh->submeshes)) {
        SDL_SetError("forge processed assets: %s reference validation data is incomplete", name);
        return false;
    }

    for (Uint32 i = 0; i < scene->mesh_count; i += 1) {
        const ForgeGpuProcessedSceneMesh *scene_mesh = &scene->meshes[i];
        const Uint32 end = scene_mesh->first_submesh + scene_mesh->submesh_count;

        if (end < scene_mesh->first_submesh || end > mesh->submesh_count) {
            SDL_SetError(
                "forge processed assets: %s scene mesh %u references submeshes outside the loaded mesh",
                name,
                (unsigned int)i);
            return false;
        }
    }

    for (Uint32 lod = 0; lod < mesh->lod_count; lod += 1) {
        for (Uint32 submesh = 0; submesh < mesh->submesh_count; submesh += 1) {
            const size_t index = (size_t)lod * mesh->submesh_count + submesh;
            const Sint32 material_index = mesh->submeshes[index].material_index;

            if (material_index < -1 ||
                (material_index >= 0 && (Uint32)material_index >= materials->material_count)) {
                SDL_SetError(
                    "forge processed assets: %s mesh LOD %u submesh %u references material %d outside the loaded material set",
                    name,
                    (unsigned int)lod,
                    (unsigned int)submesh,
                    (int)material_index);
                return false;
            }
        }
    }

    return true;
}

void ForgeGpuFreeProcessedSkins(ForgeGpuProcessedSkinSet *set)
{
    if (!set) {
        return;
    }
    for (Uint32 i = 0; i < set->skin_count; i += 1) {
        SDL_free(set->skins[i].joints);
        SDL_free(set->skins[i].inverse_bind_matrices);
    }
    SDL_free(set->skins);
    SDL_zero(*set);
}

bool ForgeGpuLoadProcessedSkins(const char *path, ForgeGpuProcessedSkinSet *set)
{
    size_t file_size = 0;
    Uint8 *file_data;
    const Uint8 *p;
    const Uint8 *end;
    Uint32 version;
    Uint32 skin_count;
    ForgeGpuProcessedSkin *skins = NULL;

    if (!path || !set) {
        SDL_SetError("forge processed assets: skin path or output storage is missing");
        return false;
    }
    SDL_zero(*set);

    file_data = (Uint8 *)SDL_LoadFile(path, &file_size);
    if (!file_data) {
        set_processed_file_error(path);
        return false;
    }
    if (file_size < FORGE_GPU_PROCESSED_FSKIN_HEADER_SIZE) {
        SDL_SetError("forge processed assets: skin '%s' is too small", path);
        goto fail;
    }

    p = file_data;
    end = file_data + file_size;
    if (SDL_memcmp(p, FORGE_GPU_PROCESSED_FSKIN_MAGIC, 4) != 0) {
        SDL_SetError("forge processed assets: skin '%s' has bad magic", path);
        goto fail;
    }
    p += 4;
    version = read_u32_le(p);
    p += 4;
    if (version != FORGE_GPU_PROCESSED_FSKIN_VERSION) {
        SDL_SetError("forge processed assets: skin '%s' uses unsupported version %u", path, version);
        goto fail;
    }
    skin_count = read_u32_le(p);
    p += 4;
    if (skin_count > FORGE_GPU_PROCESSED_FSKIN_MAX_SKINS) {
        SDL_SetError("forge processed assets: skin '%s' has too many skins", path);
        goto fail;
    }
    if (skin_count == 0) {
        SDL_free(file_data);
        return true;
    }

    skins = (ForgeGpuProcessedSkin *)SDL_calloc(skin_count, sizeof(*skins));
    if (!skins) {
        SDL_SetError("forge processed assets: skin '%s' allocation failed", path);
        goto fail;
    }

    for (Uint32 i = 0; i < skin_count; i += 1) {
        ForgeGpuProcessedSkin *skin = &skins[i];
        size_t joints_size;
        size_t matrix_count;
        size_t matrix_size;

        if ((size_t)(end - p) < FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE + 8u) {
            SDL_SetError("forge processed assets: skin '%s' is truncated at skin %u", path, (unsigned int)i);
            goto fail;
        }
        SDL_memcpy(skin->name, p, sizeof(skin->name));
        skin->name[sizeof(skin->name) - 1] = '\0';
        p += FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE;
        skin->joint_count = read_u32_le(p);
        p += 4;
        skin->skeleton = read_i32_le(p);
        p += 4;

        if (skin->joint_count > FORGE_GPU_PROCESSED_FSKIN_MAX_JOINTS) {
            SDL_SetError(
                "forge processed assets: skin '%s' skin %u has too many joints",
                path,
                (unsigned int)i);
            goto fail;
        }
        if (!checked_mul_size(skin->joint_count, sizeof(Sint32), &joints_size) ||
            !checked_mul_size(skin->joint_count, 16u, &matrix_count) ||
            !checked_mul_size(matrix_count, sizeof(float), &matrix_size)) {
            SDL_SetError("forge processed assets: skin '%s' size arithmetic overflow", path);
            goto fail;
        }

        if (skin->joint_count > 0) {
            if ((size_t)(end - p) < joints_size) {
                SDL_SetError("forge processed assets: skin '%s' is truncated at skin %u joints", path, (unsigned int)i);
                goto fail;
            }
            skin->joints = (Sint32 *)SDL_malloc(joints_size);
            if (!skin->joints) {
                SDL_SetError("forge processed assets: skin '%s' joint allocation failed", path);
                goto fail;
            }
            for (Uint32 joint = 0; joint < skin->joint_count; joint += 1) {
                skin->joints[joint] = read_i32_le(p);
                p += 4;
            }

            if ((size_t)(end - p) < matrix_size) {
                SDL_SetError(
                    "forge processed assets: skin '%s' is truncated at skin %u inverse bind matrices",
                    path,
                    (unsigned int)i);
                goto fail;
            }
            skin->inverse_bind_matrices = (float *)SDL_malloc(matrix_size);
            if (!skin->inverse_bind_matrices) {
                SDL_SetError("forge processed assets: skin '%s' inverse bind allocation failed", path);
                goto fail;
            }
            for (Uint32 f = 0; f < matrix_count; f += 1) {
                skin->inverse_bind_matrices[f] = read_f32_le(p);
                p += 4;
            }
        }
    }

    if (p != end) {
        SDL_SetError("forge processed assets: skin '%s' has trailing data", path);
        goto fail;
    }

    set->skins = skins;
    set->skin_count = skin_count;
    SDL_free(file_data);
    return true;

fail:
    if (skins) {
        ForgeGpuProcessedSkinSet tmp;
        tmp.skins = skins;
        tmp.skin_count = skin_count;
        ForgeGpuFreeProcessedSkins(&tmp);
    }
    SDL_free(file_data);
    SDL_zero(*set);
    return false;
}

bool ForgeGpuValidateProcessedSceneSkinReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedMesh *mesh,
    const ForgeGpuProcessedSkinSet *skins,
    const char *label)
{
    const char *name = label && label[0] ? label : "processed scene";
    bool scene_uses_skin = false;

    if (!scene || !mesh || !skins) {
        SDL_SetError("forge processed assets: %s skin validation arguments are missing", name);
        return false;
    }
    if ((skins->skin_count > 0 && !skins->skins) ||
        (scene->node_count > 0 && !scene->nodes)) {
        SDL_SetError("forge processed assets: %s skin validation data is incomplete", name);
        return false;
    }

    for (Uint32 i = 0; i < scene->node_count; i += 1) {
        const Sint32 skin_index = scene->nodes[i].skin_index;

        if (skin_index >= 0) {
            scene_uses_skin = true;
            if ((Uint32)skin_index >= skins->skin_count) {
                SDL_SetError(
                    "forge processed assets: %s scene node %u references skin %d outside the loaded skin set",
                    name,
                    (unsigned int)i,
                    (int)skin_index);
                return false;
            }
        }
    }

    if (scene_uses_skin && !ForgeGpuProcessedMeshIsSkinned(mesh)) {
        SDL_SetError("forge processed assets: %s scene uses skins but the loaded mesh is not skinned", name);
        return false;
    }
    if (ForgeGpuProcessedMeshIsSkinned(mesh) && skins->skin_count == 0) {
        SDL_SetError("forge processed assets: %s mesh is skinned but no skins were loaded", name);
        return false;
    }

    for (Uint32 i = 0; i < skins->skin_count; i += 1) {
        const ForgeGpuProcessedSkin *skin = &skins->skins[i];

        if (skin->joint_count > 0 && (!skin->joints || !skin->inverse_bind_matrices)) {
            SDL_SetError("forge processed assets: %s skin %u data is incomplete", name, (unsigned int)i);
            return false;
        }
        if (skin->skeleton < -1 || (skin->skeleton >= 0 && (Uint32)skin->skeleton >= scene->node_count)) {
            SDL_SetError(
                "forge processed assets: %s skin %u skeleton %d is outside the scene",
                name,
                (unsigned int)i,
                (int)skin->skeleton);
            return false;
        }
        for (Uint32 joint = 0; joint < skin->joint_count; joint += 1) {
            if (skin->joints[joint] < 0 || (Uint32)skin->joints[joint] >= scene->node_count) {
                SDL_SetError(
                    "forge processed assets: %s skin %u joint %u references node %d outside the scene",
                    name,
                    (unsigned int)i,
                    (unsigned int)joint,
                    (int)skin->joints[joint]);
                return false;
            }
        }
    }

    return true;
}

void ForgeGpuFreeProcessedAnimation(ForgeGpuProcessedAnimation *animation)
{
    if (!animation) {
        return;
    }
    for (Uint32 clip_index = 0; clip_index < animation->clip_count; clip_index += 1) {
        ForgeGpuProcessedAnimationClip *clip = &animation->clips[clip_index];

        for (Uint32 sampler_index = 0; sampler_index < clip->sampler_count; sampler_index += 1) {
            SDL_free(clip->samplers[sampler_index].times);
            SDL_free(clip->samplers[sampler_index].values);
        }
        SDL_free(clip->samplers);
        SDL_free(clip->channels);
    }
    SDL_free(animation->clips);
    SDL_zero(*animation);
}

static bool validate_animation_channel_sampler(
    const char *path,
    Uint32 clip_index,
    Uint32 channel_index,
    const ForgeGpuProcessedAnimationChannel *channel,
    const ForgeGpuProcessedAnimationClip *clip)
{
    const ForgeGpuProcessedAnimationSampler *sampler;
    Uint32 expected_components;

    if (channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION &&
        channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_ROTATION &&
        channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_SCALE &&
        channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS) {
        SDL_SetError(
            "forge processed assets: animation '%s' clip %u channel %u has invalid target path",
            path,
            (unsigned int)clip_index,
            (unsigned int)channel_index);
        return false;
    }
    if (channel->sampler_index >= clip->sampler_count) {
        SDL_SetError(
            "forge processed assets: animation '%s' clip %u channel %u references sampler %u outside the clip",
            path,
            (unsigned int)clip_index,
            (unsigned int)channel_index,
            (unsigned int)channel->sampler_index);
        return false;
    }

    sampler = &clip->samplers[channel->sampler_index];
    if (channel->target_path == FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS) {
        if (sampler->value_components == 0 ||
            sampler->value_components > FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS) {
            SDL_SetError(
                "forge processed assets: animation '%s' clip %u channel %u has invalid morph weight component count %u",
                path,
                (unsigned int)clip_index,
                (unsigned int)channel_index,
                (unsigned int)sampler->value_components);
            return false;
        }
        return true;
    }

    expected_components = channel->target_path == FORGE_GPU_PROCESSED_ANIM_PATH_ROTATION ? 4u : 3u;
    if (sampler->value_components != expected_components) {
        SDL_SetError(
            "forge processed assets: animation '%s' clip %u channel %u expects %u sampler components, got %u",
            path,
            (unsigned int)clip_index,
            (unsigned int)channel_index,
            (unsigned int)expected_components,
            (unsigned int)sampler->value_components);
        return false;
    }
    return true;
}

bool ForgeGpuLoadProcessedAnimation(const char *path, ForgeGpuProcessedAnimation *animation)
{
    size_t file_size = 0;
    Uint8 *file_data;
    const Uint8 *p;
    const Uint8 *end;
    Uint32 version;
    Uint32 clip_count;
    ForgeGpuProcessedAnimationClip *clips = NULL;

    if (!path || !animation) {
        SDL_SetError("forge processed assets: animation path or output storage is missing");
        return false;
    }
    SDL_zero(*animation);

    file_data = (Uint8 *)SDL_LoadFile(path, &file_size);
    if (!file_data) {
        set_processed_file_error(path);
        return false;
    }
    if (file_size < FORGE_GPU_PROCESSED_FANIM_HEADER_SIZE) {
        SDL_SetError("forge processed assets: animation '%s' is too small", path);
        goto fail;
    }

    p = file_data;
    end = file_data + file_size;
    if (SDL_memcmp(p, FORGE_GPU_PROCESSED_FANIM_MAGIC, 4) != 0) {
        SDL_SetError("forge processed assets: animation '%s' has bad magic", path);
        goto fail;
    }
    p += 4;
    version = read_u32_le(p);
    p += 4;
    if (version != FORGE_GPU_PROCESSED_FANIM_VERSION) {
        SDL_SetError("forge processed assets: animation '%s' uses unsupported version %u", path, version);
        goto fail;
    }
    clip_count = read_u32_le(p);
    p += 4;
    if (clip_count > FORGE_GPU_PROCESSED_FANIM_MAX_CLIPS) {
        SDL_SetError("forge processed assets: animation '%s' has too many clips", path);
        goto fail;
    }
    if (clip_count == 0) {
        SDL_free(file_data);
        return true;
    }

    clips = (ForgeGpuProcessedAnimationClip *)SDL_calloc(clip_count, sizeof(*clips));
    if (!clips) {
        SDL_SetError("forge processed assets: animation '%s' allocation failed", path);
        goto fail;
    }

    for (Uint32 clip_index = 0; clip_index < clip_count; clip_index += 1) {
        ForgeGpuProcessedAnimationClip *clip = &clips[clip_index];

        if ((size_t)(end - p) < FORGE_GPU_PROCESSED_FANIM_NAME_SIZE + 12u) {
            SDL_SetError("forge processed assets: animation '%s' is truncated at clip %u", path, (unsigned int)clip_index);
            goto fail;
        }
        SDL_memcpy(clip->name, p, sizeof(clip->name));
        clip->name[sizeof(clip->name) - 1] = '\0';
        p += FORGE_GPU_PROCESSED_FANIM_NAME_SIZE;
        clip->duration = read_f32_le(p);
        p += 4;
        clip->sampler_count = read_u32_le(p);
        p += 4;
        clip->channel_count = read_u32_le(p);
        p += 4;

        if (clip->sampler_count > FORGE_GPU_PROCESSED_FANIM_MAX_SAMPLERS ||
            clip->channel_count > FORGE_GPU_PROCESSED_FANIM_MAX_CHANNELS ||
            clip->duration < 0.0f) {
            SDL_SetError("forge processed assets: animation '%s' clip %u header is invalid", path, (unsigned int)clip_index);
            goto fail;
        }

        if (clip->sampler_count > 0) {
            clip->samplers = (ForgeGpuProcessedAnimationSampler *)SDL_calloc(clip->sampler_count, sizeof(*clip->samplers));
            if (!clip->samplers) {
                SDL_SetError("forge processed assets: animation '%s' sampler allocation failed", path);
                goto fail;
            }
        }

        for (Uint32 sampler_index = 0; sampler_index < clip->sampler_count; sampler_index += 1) {
            ForgeGpuProcessedAnimationSampler *sampler = &clip->samplers[sampler_index];
            size_t timestamp_size;
            size_t value_count;
            size_t value_size;

            if ((size_t)(end - p) < 12u) {
                SDL_SetError(
                    "forge processed assets: animation '%s' is truncated at clip %u sampler %u",
                    path,
                    (unsigned int)clip_index,
                    (unsigned int)sampler_index);
                goto fail;
            }
            sampler->keyframe_count = read_u32_le(p);
            p += 4;
            sampler->value_components = read_u32_le(p);
            p += 4;
            sampler->interpolation = read_u32_le(p);
            p += 4;

            if (sampler->keyframe_count > FORGE_GPU_PROCESSED_FANIM_MAX_KEYFRAMES ||
                sampler->value_components == 0 ||
                sampler->value_components > FORGE_GPU_PROCESSED_FANIM_MAX_VALUE_COMPONENTS ||
                (sampler->interpolation != FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_LINEAR &&
                 sampler->interpolation != FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_STEP)) {
                SDL_SetError(
                    "forge processed assets: animation '%s' clip %u sampler %u header is invalid",
                    path,
                    (unsigned int)clip_index,
                    (unsigned int)sampler_index);
                goto fail;
            }
            if (!checked_mul_size(sampler->keyframe_count, sizeof(float), &timestamp_size) ||
                !checked_mul_size(sampler->keyframe_count, sampler->value_components, &value_count) ||
                !checked_mul_size(value_count, sizeof(float), &value_size)) {
                SDL_SetError("forge processed assets: animation '%s' size arithmetic overflow", path);
                goto fail;
            }

            if (sampler->keyframe_count > 0) {
                if ((size_t)(end - p) < timestamp_size) {
                    SDL_SetError(
                        "forge processed assets: animation '%s' is truncated at clip %u sampler %u timestamps",
                        path,
                        (unsigned int)clip_index,
                        (unsigned int)sampler_index);
                    goto fail;
                }
                sampler->times = (float *)SDL_malloc(timestamp_size);
                if (!sampler->times) {
                    SDL_SetError("forge processed assets: animation '%s' timestamp allocation failed", path);
                    goto fail;
                }
                for (Uint32 key = 0; key < sampler->keyframe_count; key += 1) {
                    sampler->times[key] = read_f32_le(p);
                    p += 4;
                }

                if ((size_t)(end - p) < value_size) {
                    SDL_SetError(
                        "forge processed assets: animation '%s' is truncated at clip %u sampler %u values",
                        path,
                        (unsigned int)clip_index,
                        (unsigned int)sampler_index);
                    goto fail;
                }
                sampler->values = (float *)SDL_malloc(value_size);
                if (!sampler->values) {
                    SDL_SetError("forge processed assets: animation '%s' value allocation failed", path);
                    goto fail;
                }
                for (size_t value = 0; value < value_count; value += 1) {
                    sampler->values[value] = read_f32_le(p);
                    p += 4;
                }
            }
        }

        if (clip->channel_count > 0) {
            clip->channels = (ForgeGpuProcessedAnimationChannel *)SDL_calloc(clip->channel_count, sizeof(*clip->channels));
            if (!clip->channels) {
                SDL_SetError("forge processed assets: animation '%s' channel allocation failed", path);
                goto fail;
            }
        }
        for (Uint32 channel_index = 0; channel_index < clip->channel_count; channel_index += 1) {
            ForgeGpuProcessedAnimationChannel *channel = &clip->channels[channel_index];

            if ((size_t)(end - p) < 12u) {
                SDL_SetError(
                    "forge processed assets: animation '%s' is truncated at clip %u channel %u",
                    path,
                    (unsigned int)clip_index,
                    (unsigned int)channel_index);
                goto fail;
            }
            channel->target_node = read_i32_le(p);
            p += 4;
            channel->target_path = read_u32_le(p);
            p += 4;
            channel->sampler_index = read_u32_le(p);
            p += 4;

            if (!validate_animation_channel_sampler(path, clip_index, channel_index, channel, clip)) {
                goto fail;
            }
        }
    }

    if (p != end) {
        SDL_SetError("forge processed assets: animation '%s' has trailing data", path);
        goto fail;
    }

    animation->clips = clips;
    animation->clip_count = clip_count;
    SDL_free(file_data);
    return true;

fail:
    if (clips) {
        ForgeGpuProcessedAnimation tmp;
        tmp.clips = clips;
        tmp.clip_count = clip_count;
        ForgeGpuFreeProcessedAnimation(&tmp);
    }
    SDL_free(file_data);
    SDL_zero(*animation);
    return false;
}

bool ForgeGpuValidateProcessedSceneAnimationReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedAnimation *animation,
    const char *label)
{
    const char *name = label && label[0] ? label : "processed animation";

    if (!scene || !animation) {
        SDL_SetError("forge processed assets: %s animation validation arguments are missing", name);
        return false;
    }
    if ((scene->node_count > 0 && !scene->nodes) ||
        (animation->clip_count > 0 && !animation->clips)) {
        SDL_SetError("forge processed assets: %s animation validation data is incomplete", name);
        return false;
    }

    for (Uint32 clip_index = 0; clip_index < animation->clip_count; clip_index += 1) {
        const ForgeGpuProcessedAnimationClip *clip = &animation->clips[clip_index];

        if (clip->channel_count > 0 && !clip->channels) {
            SDL_SetError(
                "forge processed assets: %s clip %u has incomplete channels",
                name,
                (unsigned int)clip_index);
            return false;
        }
        for (Uint32 channel_index = 0; channel_index < clip->channel_count; channel_index += 1) {
            const ForgeGpuProcessedAnimationChannel *channel = &clip->channels[channel_index];

            if (channel->target_node < 0 || (Uint32)channel->target_node >= scene->node_count) {
                SDL_SetError(
                    "forge processed assets: %s clip %u channel %u references node %d outside the scene",
                    name,
                    (unsigned int)clip_index,
                    (unsigned int)channel_index,
                    (int)channel->target_node);
                return false;
            }
            if (channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS &&
                !scene->nodes[channel->target_node].has_trs) {
                SDL_SetError(
                    "forge processed assets: %s clip %u channel %u targets matrix-only scene node %d",
                    name,
                    (unsigned int)clip_index,
                    (unsigned int)channel_index,
                    (int)channel->target_node);
                return false;
            }
        }
    }

    return true;
}

static Vec3 vec3_from_float3(const float values[3])
{
    Vec3 result = { values[0], values[1], values[2] };
    return result;
}

static Quat quat_from_processed_xyzw(const float values[4])
{
    return quat_create(values[3], values[0], values[1], values[2]);
}

static void write_vec3_to_float3(float out[3], Vec3 value)
{
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

static void write_quat_to_processed_xyzw(float out[4], Quat value)
{
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
    out[3] = value.w;
}

static Mat4 processed_mat4_from_array(const float values[16])
{
    Mat4 result;

    SDL_memcpy(result.m, values, sizeof(result.m));
    return result;
}

static void write_mat4_to_array(float out[16], Mat4 matrix)
{
    SDL_memcpy(out, matrix.m, sizeof(matrix.m));
}

static int find_processed_animation_keyframe(const float *times, Uint32 count, float time_seconds)
{
    int lo = 0;
    int hi = (int)count - 1;

    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;

        if (times[mid] <= time_seconds) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static float normalize_processed_animation_time(
    const ForgeGpuProcessedAnimationClip *clip,
    float time_seconds,
    bool loop)
{
    if (time_seconds != time_seconds || time_seconds > FLT_MAX || time_seconds < -FLT_MAX) {
        time_seconds = 0.0f;
    }
    if (clip->duration <= 1e-7f) {
        return 0.0f;
    }
    if (loop) {
        time_seconds = SDL_fmodf(time_seconds, clip->duration);
        if (time_seconds < 0.0f) {
            time_seconds += clip->duration;
        }
    } else {
        if (time_seconds < 0.0f) {
            time_seconds = 0.0f;
        }
        if (time_seconds > clip->duration) {
            time_seconds = clip->duration;
        }
    }
    return time_seconds;
}

static Vec3 eval_processed_vec3_sampler(
    const ForgeGpuProcessedAnimationSampler *sampler,
    float time_seconds)
{
    const float *a;
    const float *b;
    int lo;
    float span;
    float alpha;

    if (sampler->keyframe_count == 0) {
        Vec3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
    if (time_seconds <= sampler->times[0]) {
        return vec3_from_float3(sampler->values);
    }
    if (time_seconds >= sampler->times[sampler->keyframe_count - 1]) {
        return vec3_from_float3(sampler->values + (size_t)(sampler->keyframe_count - 1) * 3u);
    }

    lo = find_processed_animation_keyframe(sampler->times, sampler->keyframe_count, time_seconds);
    a = sampler->values + (size_t)lo * 3u;
    if (sampler->interpolation == FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_STEP) {
        return vec3_from_float3(a);
    }

    b = sampler->values + (size_t)(lo + 1) * 3u;
    span = sampler->times[lo + 1] - sampler->times[lo];
    alpha = span > 1e-7f ? (time_seconds - sampler->times[lo]) / span : 0.0f;
    return vec3_lerp(vec3_from_float3(a), vec3_from_float3(b), alpha);
}

static Quat eval_processed_quat_sampler(
    const ForgeGpuProcessedAnimationSampler *sampler,
    float time_seconds)
{
    const float *a;
    const float *b;
    int lo;
    float span;
    float alpha;

    if (sampler->keyframe_count == 0) {
        return quat_create(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (time_seconds <= sampler->times[0]) {
        return quat_from_processed_xyzw(sampler->values);
    }
    if (time_seconds >= sampler->times[sampler->keyframe_count - 1]) {
        return quat_from_processed_xyzw(sampler->values + (size_t)(sampler->keyframe_count - 1) * 4u);
    }

    lo = find_processed_animation_keyframe(sampler->times, sampler->keyframe_count, time_seconds);
    a = sampler->values + (size_t)lo * 4u;
    if (sampler->interpolation == FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_STEP) {
        return quat_from_processed_xyzw(a);
    }

    b = sampler->values + (size_t)(lo + 1) * 4u;
    span = sampler->times[lo + 1] - sampler->times[lo];
    alpha = span > 1e-7f ? (time_seconds - sampler->times[lo]) / span : 0.0f;
    return quat_slerp(quat_from_processed_xyzw(a), quat_from_processed_xyzw(b), alpha);
}

static void rebuild_processed_node_local_transform(ForgeGpuProcessedSceneNode *node)
{
    const Mat4 translation = mat4_translate(vec3_from_float3(node->translation));
    const Mat4 rotation = quat_to_mat4(quat_from_processed_xyzw(node->rotation));
    const Mat4 scale = mat4_scale_vec3(vec3_from_float3(node->scale));

    write_mat4_to_array(node->local_transform, mat4_multiply(translation, mat4_multiply(rotation, scale)));
}

bool ForgeGpuEvaluateProcessedMorphWeights(
    const ForgeGpuProcessedAnimationClip *clip,
    Sint32 target_node,
    float time_seconds,
    bool loop,
    float *weights,
    Uint32 weight_count)
{
    if (!clip || !weights || weight_count == 0 ||
        weight_count > FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS) {
        SDL_SetError("forge processed assets: invalid morph weight evaluation arguments");
        return false;
    }
    if (clip->channel_count == 0) {
        return true;
    }

    time_seconds = normalize_processed_animation_time(clip, time_seconds, loop);

    for (Uint32 channel_index = 0; channel_index < clip->channel_count; channel_index += 1) {
        const ForgeGpuProcessedAnimationChannel *channel = &clip->channels[channel_index];
        const ForgeGpuProcessedAnimationSampler *sampler;
        const float *a;
        const float *b;
        int lo;
        float span;
        float alpha;

        if (channel->target_path != FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS ||
            channel->target_node != target_node) {
            continue;
        }
        if (channel->sampler_index >= clip->sampler_count) {
            SDL_SetError("forge processed assets: morph weight animation channel references an invalid sampler");
            return false;
        }

        sampler = &clip->samplers[channel->sampler_index];
        if (sampler->value_components != weight_count) {
            SDL_SetError(
                "forge processed assets: morph weight sampler has %u components, expected %u",
                (unsigned int)sampler->value_components,
                (unsigned int)weight_count);
            return false;
        }
        if (!sampler->times || !sampler->values || sampler->keyframe_count == 0) {
            continue;
        }

        if (time_seconds <= sampler->times[0]) {
            SDL_memcpy(weights, sampler->values, sizeof(float) * weight_count);
            continue;
        }
        if (time_seconds >= sampler->times[sampler->keyframe_count - 1]) {
            SDL_memcpy(
                weights,
                sampler->values + (size_t)(sampler->keyframe_count - 1) * weight_count,
                sizeof(float) * weight_count);
            continue;
        }
        if (sampler->keyframe_count < 2) {
            continue;
        }

        lo = find_processed_animation_keyframe(sampler->times, sampler->keyframe_count, time_seconds);
        a = sampler->values + (size_t)lo * weight_count;
        if (sampler->interpolation == FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_STEP) {
            SDL_memcpy(weights, a, sizeof(float) * weight_count);
            continue;
        }

        b = sampler->values + (size_t)(lo + 1) * weight_count;
        span = sampler->times[lo + 1] - sampler->times[lo];
        alpha = span > 1e-7f ? (time_seconds - sampler->times[lo]) / span : 0.0f;
        for (Uint32 weight_index = 0; weight_index < weight_count; weight_index += 1) {
            weights[weight_index] = a[weight_index] + (b[weight_index] - a[weight_index]) * alpha;
        }
    }

    return true;
}

bool ForgeGpuApplyProcessedSceneAnimation(
    ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedAnimationClip *clip,
    float time_seconds,
    bool loop)
{
    Uint8 *modified;

    if (!scene || !scene->nodes || !clip) {
        SDL_SetError("forge processed assets: invalid animation apply arguments");
        return false;
    }
    if (scene->node_count == 0 || clip->channel_count == 0) {
        return true;
    }

    time_seconds = normalize_processed_animation_time(clip, time_seconds, loop);
    modified = (Uint8 *)SDL_calloc(scene->node_count, sizeof(*modified));
    if (!modified) {
        SDL_OutOfMemory();
        return false;
    }

    for (Uint32 channel_index = 0; channel_index < clip->channel_count; channel_index += 1) {
        const ForgeGpuProcessedAnimationChannel *channel = &clip->channels[channel_index];
        const ForgeGpuProcessedAnimationSampler *sampler;
        ForgeGpuProcessedSceneNode *node;

        if (channel->target_node < 0 || (Uint32)channel->target_node >= scene->node_count ||
            channel->sampler_index >= clip->sampler_count) {
            continue;
        }
        sampler = &clip->samplers[channel->sampler_index];
        if (!sampler->times || !sampler->values || sampler->keyframe_count == 0) {
            continue;
        }
        if (channel->target_path == FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS) {
            continue;
        }

        node = &scene->nodes[channel->target_node];
        if (!node->has_trs) {
            SDL_free(modified);
            SDL_SetError("forge processed assets: animation targets a matrix-only scene node");
            return false;
        }

        switch (channel->target_path) {
        case FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION:
            if (sampler->value_components != 3u) {
                SDL_free(modified);
                SDL_SetError("forge processed assets: translation animation sampler has wrong component count");
                return false;
            }
            write_vec3_to_float3(node->translation, eval_processed_vec3_sampler(sampler, time_seconds));
            break;
        case FORGE_GPU_PROCESSED_ANIM_PATH_ROTATION:
            if (sampler->value_components != 4u) {
                SDL_free(modified);
                SDL_SetError("forge processed assets: rotation animation sampler has wrong component count");
                return false;
            }
            write_quat_to_processed_xyzw(node->rotation, eval_processed_quat_sampler(sampler, time_seconds));
            break;
        case FORGE_GPU_PROCESSED_ANIM_PATH_SCALE:
            if (sampler->value_components != 3u) {
                SDL_free(modified);
                SDL_SetError("forge processed assets: scale animation sampler has wrong component count");
                return false;
            }
            write_vec3_to_float3(node->scale, eval_processed_vec3_sampler(sampler, time_seconds));
            break;
        default:
            SDL_free(modified);
            SDL_SetError("forge processed assets: animation has unsupported target path %u", (unsigned int)channel->target_path);
            return false;
        }

        modified[channel->target_node] = 1u;
    }

    for (Uint32 node_index = 0; node_index < scene->node_count; node_index += 1) {
        if (modified[node_index]) {
            rebuild_processed_node_local_transform(&scene->nodes[node_index]);
        }
    }

    SDL_free(modified);
    return true;
}

bool ForgeGpuComputeProcessedSkinJointMatrices(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedSkin *skin,
    Sint32 mesh_node_index,
    float *out_matrices,
    Uint32 matrix_capacity,
    Uint32 *out_matrix_count)
{
    Mat4 inverse_mesh_world = mat4_identity();
    Uint32 count;

    if (!scene || !skin || !out_matrices || !out_matrix_count ||
        (scene->node_count > 0 && !scene->nodes)) {
        SDL_SetError("forge processed assets: invalid skin joint matrix arguments");
        return false;
    }
    if (skin->joint_count > 0 && (!skin->joints || !skin->inverse_bind_matrices)) {
        SDL_SetError("forge processed assets: skin joint palette data is incomplete");
        return false;
    }
    if (skin->joint_count > matrix_capacity) {
        SDL_SetError("forge processed assets: skin joint matrix output capacity is too small");
        return false;
    }

    if (mesh_node_index >= 0 && (Uint32)mesh_node_index < scene->node_count) {
        inverse_mesh_world = mat4_inverse(processed_mat4_from_array(scene->nodes[mesh_node_index].world_transform));
    } else if (mesh_node_index >= 0) {
        SDL_SetError("forge processed assets: skinned mesh node index is out of range");
        return false;
    }

    count = skin->joint_count;
    for (Uint32 i = 0; i < count; i += 1) {
        const Sint32 joint_index = skin->joints[i];
        Mat4 joint_world;
        Mat4 inverse_bind;
        Mat4 joint_matrix;

        if (joint_index < 0 || (Uint32)joint_index >= scene->node_count) {
            SDL_SetError("forge processed assets: skin joint index is out of range");
            return false;
        }

        joint_world = processed_mat4_from_array(scene->nodes[joint_index].world_transform);
        inverse_bind = processed_mat4_from_array(&skin->inverse_bind_matrices[(size_t)i * 16u]);
        joint_matrix = mat4_multiply(inverse_mesh_world, mat4_multiply(joint_world, inverse_bind));
        SDL_memcpy(out_matrices + (size_t)i * 16u, joint_matrix.m, sizeof(joint_matrix.m));
    }

    *out_matrix_count = count;
    return true;
}

bool ForgeGpuLoadProcessedSceneV1(const char *path, ForgeGpuProcessedScene *scene)
{
    /* Header, roots, mesh table, fixed-size nodes, then the flat child array. */
    size_t file_size = 0;
    Uint8 *file_data;
    const Uint8 *p;
    Uint32 version;
    Uint32 node_count;
    Uint32 mesh_count;
    Uint32 root_count;
    Uint32 reserved;
    size_t roots_size;
    size_t mesh_table_size;
    size_t node_table_size;
    size_t min_size;
    Uint32 child_count = 0;
    Uint64 child_bytes;
    Uint64 child_end;
    Uint32 *roots = NULL;
    ForgeGpuProcessedSceneMesh *meshes = NULL;
    ForgeGpuProcessedSceneNode *nodes = NULL;
    Uint32 *children = NULL;
    Uint8 *visited = NULL;
    float identity[16];

    if (!path || !scene) {
        SDL_SetError("forge processed assets: scene path or output storage is missing");
        return false;
    }
    SDL_zero(*scene);

    file_data = (Uint8 *)SDL_LoadFile(path, &file_size);
    if (!file_data) {
        set_processed_file_error(path);
        return false;
    }
    if (file_size < FORGE_GPU_PROCESSED_FSCENE_HEADER_SIZE) {
        SDL_SetError("forge processed assets: scene '%s' is too small", path);
        goto fail;
    }

    p = file_data;
    if (SDL_memcmp(p, FORGE_GPU_PROCESSED_FSCENE_MAGIC, 4) != 0) {
        SDL_SetError("forge processed assets: scene '%s' has bad magic", path);
        goto fail;
    }
    p += 4;
    version = read_u32_le(p);
    p += 4;
    if (version != FORGE_GPU_PROCESSED_FSCENE_VERSION) {
        SDL_SetError("forge processed assets: scene '%s' uses unsupported version %u", path, version);
        goto fail;
    }

    node_count = read_u32_le(p);
    p += 4;
    mesh_count = read_u32_le(p);
    p += 4;
    root_count = read_u32_le(p);
    p += 4;
    reserved = read_u32_le(p);
    p += 4;
    if (reserved != 0) {
        SDL_SetError("forge processed assets: scene '%s' has nonzero reserved header data", path);
        goto fail;
    }

    if (node_count > FORGE_GPU_PROCESSED_FSCENE_MAX_NODES ||
        mesh_count > FORGE_GPU_PROCESSED_FSCENE_MAX_MESHES ||
        root_count > FORGE_GPU_PROCESSED_FSCENE_MAX_ROOTS) {
        SDL_SetError("forge processed assets: scene '%s' header counts exceed limits", path);
        goto fail;
    }

    if (!checked_mul_size(root_count, sizeof(Uint32), &roots_size) ||
        !checked_mul_size(mesh_count, 8u, &mesh_table_size) ||
        !checked_mul_size(node_count, FORGE_GPU_PROCESSED_FSCENE_NODE_SIZE, &node_table_size) ||
        !checked_add_size(FORGE_GPU_PROCESSED_FSCENE_HEADER_SIZE, roots_size, &min_size) ||
        !checked_add_size(min_size, mesh_table_size, &min_size) ||
        !checked_add_size(min_size, node_table_size, &min_size)) {
        SDL_SetError("forge processed assets: scene '%s' size arithmetic overflow", path);
        goto fail;
    }
    if (file_size < min_size) {
        SDL_SetError("forge processed assets: scene '%s' is truncated before children", path);
        goto fail;
    }

    if (root_count > 0) {
        roots = (Uint32 *)SDL_malloc((size_t)root_count * sizeof(*roots));
        if (!roots) {
            SDL_SetError("forge processed assets: scene '%s' root allocation failed", path);
            goto fail;
        }
        for (Uint32 i = 0; i < root_count; i += 1) {
            roots[i] = read_u32_le(p);
            p += 4;
            if (roots[i] >= node_count) {
                SDL_SetError("forge processed assets: scene '%s' root %u is out of range", path, (unsigned int)i);
                goto fail;
            }
        }
    }

    if (mesh_count > 0) {
        meshes = (ForgeGpuProcessedSceneMesh *)SDL_malloc((size_t)mesh_count * sizeof(*meshes));
        if (!meshes) {
            SDL_SetError("forge processed assets: scene '%s' mesh table allocation failed", path);
            goto fail;
        }
        for (Uint32 i = 0; i < mesh_count; i += 1) {
            Uint32 end;

            meshes[i].first_submesh = read_u32_le(p);
            p += 4;
            meshes[i].submesh_count = read_u32_le(p);
            p += 4;
            end = meshes[i].first_submesh + meshes[i].submesh_count;
            if (end < meshes[i].first_submesh ||
                end > FORGE_GPU_PROCESSED_FMESH_MAX_SUBMESHES) {
                SDL_SetError("forge processed assets: scene '%s' mesh %u submesh range is invalid", path, (unsigned int)i);
                goto fail;
            }
        }
    }

    if (node_count > 0) {
        nodes = (ForgeGpuProcessedSceneNode *)SDL_calloc(node_count, sizeof(*nodes));
        if (!nodes) {
            SDL_SetError("forge processed assets: scene '%s' node allocation failed", path);
            goto fail;
        }
        for (Uint32 i = 0; i < node_count; i += 1) {
            ForgeGpuProcessedSceneNode *node = &nodes[i];
            Uint32 has_trs;

            SDL_memcpy(node->name, p, sizeof(node->name));
            node->name[sizeof(node->name) - 1] = '\0';
            p += 64;

            node->parent = read_i32_le(p);
            p += 4;
            node->mesh_index = read_i32_le(p);
            p += 4;
            node->skin_index = read_i32_le(p);
            p += 4;
            if (node->parent < -1 ||
                (node->parent >= 0 && (Uint32)node->parent >= node_count)) {
                SDL_SetError("forge processed assets: scene '%s' node %u parent is out of range", path, (unsigned int)i);
                goto fail;
            }
            if (node->mesh_index < -1 ||
                (node->mesh_index >= 0 && (Uint32)node->mesh_index >= mesh_count)) {
                SDL_SetError("forge processed assets: scene '%s' node %u mesh index is out of range", path, (unsigned int)i);
                goto fail;
            }
            if (node->skin_index < -1) {
                SDL_SetError("forge processed assets: scene '%s' node %u skin index is invalid", path, (unsigned int)i);
                goto fail;
            }

            node->first_child = read_u32_le(p);
            p += 4;
            node->child_count = read_u32_le(p);
            p += 4;
            has_trs = read_u32_le(p);
            p += 4;
            if (has_trs > 1) {
                SDL_SetError("forge processed assets: scene '%s' node %u has invalid TRS flag", path, (unsigned int)i);
                goto fail;
            }
            node->has_trs = has_trs != 0;

            for (int j = 0; j < 3; j += 1) {
                node->translation[j] = read_f32_le(p);
                p += 4;
            }
            for (int j = 0; j < 4; j += 1) {
                node->rotation[j] = read_f32_le(p);
                p += 4;
            }
            for (int j = 0; j < 3; j += 1) {
                node->scale[j] = read_f32_le(p);
                p += 4;
            }
            for (int j = 0; j < 16; j += 1) {
                node->local_transform[j] = read_f32_le(p);
                p += 4;
            }

            if (child_count > SDL_MAX_UINT32 - node->child_count) {
                SDL_SetError("forge processed assets: scene '%s' child count overflow", path);
                goto fail;
            }
            child_count += node->child_count;
        }

        const Uint32 max_edges = node_count > root_count ? node_count - root_count : 0;
        if (child_count > max_edges) {
            SDL_SetError("forge processed assets: scene '%s' has too many child links", path);
            goto fail;
        }
        for (Uint32 i = 0; i < node_count; i += 1) {
            const Uint32 end = nodes[i].first_child + nodes[i].child_count;
            if (nodes[i].child_count > 0 &&
                (end < nodes[i].first_child || end > child_count)) {
                SDL_SetError("forge processed assets: scene '%s' node %u child range is invalid", path, (unsigned int)i);
                goto fail;
            }
        }
    }

    child_bytes = (Uint64)child_count * sizeof(Uint32);
    child_end = (Uint64)(p - file_data) + child_bytes;
    if (child_end != (Uint64)file_size) {
        SDL_SetError(
            "forge processed assets: scene '%s' has %s child data",
            path,
            child_end > (Uint64)file_size ? "truncated" : "trailing");
        goto fail;
    }
    if (child_count > 0) {
        children = (Uint32 *)SDL_malloc((size_t)child_count * sizeof(*children));
        if (!children) {
            SDL_SetError("forge processed assets: scene '%s' children allocation failed", path);
            goto fail;
        }
        for (Uint32 i = 0; i < child_count; i += 1) {
            children[i] = read_u32_le(p);
            p += 4;
            if (children[i] >= node_count) {
                SDL_SetError("forge processed assets: scene '%s' child %u is out of range", path, (unsigned int)i);
                goto fail;
            }
        }
    }

    scene->nodes = nodes;
    scene->node_count = node_count;
    scene->meshes = meshes;
    scene->mesh_count = mesh_count;
    scene->roots = roots;
    scene->root_count = root_count;
    scene->children = children;
    scene->child_count = child_count;
    nodes = NULL;
    meshes = NULL;
    roots = NULL;
    children = NULL;

    if (node_count > 0) {
        visited = (Uint8 *)SDL_calloc(node_count, sizeof(*visited));
        if (!visited) {
            SDL_SetError("forge processed assets: scene '%s' visited allocation failed", path);
            goto fail_loaded;
        }
        mat4_identity(identity);
        for (Uint32 i = 0; i < root_count; i += 1) {
            if (!compute_scene_world_transforms(scene, scene->roots[i], -1, identity, 0, visited)) {
                goto fail_loaded;
            }
        }
        for (Uint32 i = 0; i < node_count; i += 1) {
            if (!visited[i]) {
                SDL_SetError("forge processed assets: scene '%s' node %u is unreachable", path, (unsigned int)i);
                goto fail_loaded;
            }
        }
    }

    SDL_free(visited);
    SDL_free(file_data);
    return true;

fail_loaded:
    SDL_free(visited);
    ForgeGpuFreeProcessedScene(scene);
    SDL_free(file_data);
    return false;

fail:
    SDL_free(children);
    SDL_free(nodes);
    SDL_free(meshes);
    SDL_free(roots);
    SDL_free(file_data);
    SDL_zero(*scene);
    return false;
}

void ForgeGpuFreeProcessedCompressedTexture(ForgeGpuProcessedCompressedTexture *texture)
{
    if (!texture) {
        return;
    }
    SDL_free(texture->file_data);
    SDL_free(texture->mips);
    SDL_zero(*texture);
}

bool ForgeGpuLoadProcessedFtexV1(const char *path, ForgeGpuProcessedCompressedTexture *texture)
{
    Uint8 *file_data;
    size_t file_size = 0;
    ForgeGpuProcessedCompressedMip *mips = NULL;
    size_t mip_table_size;
    size_t mip_table_end;
    Uint64 expected_mip_offset;
    Uint32 magic;
    Uint32 version;
    Uint32 format;
    Uint32 width;
    Uint32 height;
    Uint32 mip_count;
    Uint32 reserved0;
    Uint32 reserved1;
    Uint32 max_mips = 1;

    if (!path || !texture) {
        SDL_SetError("forge processed assets: .ftex path or output texture is missing");
        return false;
    }
    SDL_zero(*texture);

    file_data = (Uint8 *)SDL_LoadFile(path, &file_size);
    if (!file_data) {
        set_processed_file_error(path);
        return false;
    }
    if (file_size < FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE) {
        SDL_SetError("forge processed assets: .ftex '%s' is too small for a header", path);
        goto fail;
    }

    magic = read_u32_le(file_data);
    version = read_u32_le(file_data + 4);
    format = read_u32_le(file_data + 8);
    width = read_u32_le(file_data + 12);
    height = read_u32_le(file_data + 16);
    mip_count = read_u32_le(file_data + 20);
    reserved0 = read_u32_le(file_data + 24);
    reserved1 = read_u32_le(file_data + 28);

    if (magic != FORGE_GPU_PROCESSED_FTEX_MAGIC) {
        SDL_SetError("forge processed assets: .ftex '%s' has bad magic", path);
        goto fail;
    }
    if (version != FORGE_GPU_PROCESSED_FTEX_VERSION) {
        SDL_SetError("forge processed assets: .ftex '%s' has unsupported version %u", path, (unsigned int)version);
        goto fail;
    }
    if (format < FORGE_GPU_PROCESSED_FTEX_BC7_SRGB || format > FORGE_GPU_PROCESSED_FTEX_BC5_UNORM) {
        SDL_SetError("forge processed assets: .ftex '%s' has unsupported format %u", path, (unsigned int)format);
        goto fail;
    }
    if (width == 0 || height == 0) {
        SDL_SetError("forge processed assets: .ftex '%s' has invalid dimensions %ux%u", path, (unsigned int)width, (unsigned int)height);
        goto fail;
    }
    if (mip_count == 0 || mip_count > FORGE_GPU_PROCESSED_FTEX_MAX_MIP_LEVELS) {
        SDL_SetError("forge processed assets: .ftex '%s' has invalid mip count %u", path, (unsigned int)mip_count);
        goto fail;
    }
    /* Forge's v1 writer leaves these fields zero; reject reuse until the format version changes. */
    if (reserved0 != 0 || reserved1 != 0) {
        SDL_SetError("forge processed assets: .ftex '%s' has nonzero reserved header fields", path);
        goto fail;
    }

    for (Uint32 dim = SDL_max(width, height); dim > 1; dim >>= 1) {
        max_mips += 1;
    }
    if (mip_count > max_mips) {
        SDL_SetError(
            "forge processed assets: .ftex '%s' has %u mips for %ux%u base dimensions",
            path,
            (unsigned int)mip_count,
            (unsigned int)width,
            (unsigned int)height);
        goto fail;
    }

    if (!checked_mul_size((size_t)mip_count, FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE, &mip_table_size) ||
        !checked_add_size(FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE, mip_table_size, &mip_table_end) ||
        file_size < mip_table_end) {
        SDL_SetError("forge processed assets: .ftex '%s' has a truncated mip table", path);
        goto fail;
    }

    mips = (ForgeGpuProcessedCompressedMip *)SDL_calloc(mip_count, sizeof(*mips));
    if (!mips) {
        SDL_SetError("forge processed assets: failed to allocate .ftex mip table");
        goto fail;
    }

    /* The Forge texture tool writes mip payloads contiguously after the table. */
    expected_mip_offset = (Uint64)mip_table_end;
    for (Uint32 i = 0; i < mip_count; i += 1) {
        const Uint8 *entry = file_data + FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + ((size_t)i * FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE);
        Uint32 mip_offset = read_u32_le(entry);
        Uint32 mip_size = read_u32_le(entry + 4);
        Uint32 mip_width = read_u32_le(entry + 8);
        Uint32 mip_height = read_u32_le(entry + 12);
        Uint32 expected_width = width >> i;
        Uint32 expected_height = height >> i;
        Uint64 blocks_x;
        Uint64 blocks_y;
        Uint64 block_count;
        Uint64 minimum_size;
        Uint64 mip_end;

        if (expected_width == 0) {
            expected_width = 1;
        }
        if (expected_height == 0) {
            expected_height = 1;
        }
        blocks_x = ((Uint64)expected_width + 3u) / 4u;
        blocks_y = ((Uint64)expected_height + 3u) / 4u;
        if (blocks_x != 0 && blocks_y > SDL_MAX_UINT64 / blocks_x) {
            SDL_SetError("forge processed assets: .ftex '%s' mip %u dimensions overflow", path, (unsigned int)i);
            goto fail;
        }
        block_count = blocks_x * blocks_y;
        if (block_count > SDL_MAX_UINT64 / FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK) {
            SDL_SetError("forge processed assets: .ftex '%s' mip %u BC byte size overflows", path, (unsigned int)i);
            goto fail;
        }
        minimum_size = block_count * FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK;
        mip_end = (Uint64)mip_offset + (Uint64)mip_size;

        if (mip_width != expected_width ||
            mip_height != expected_height ||
            mip_offset != expected_mip_offset ||
            mip_end > (Uint64)file_size ||
            (Uint64)mip_size < minimum_size) {
            SDL_SetError(
                "forge processed assets: .ftex '%s' mip %u is invalid",
                path,
                (unsigned int)i);
            goto fail;
        }

        mips[i].data = file_data + mip_offset;
        mips[i].data_size = mip_size;
        mips[i].width = mip_width;
        mips[i].height = mip_height;
        expected_mip_offset = mip_end;
    }
    if (expected_mip_offset != (Uint64)file_size) {
        SDL_SetError("forge processed assets: .ftex '%s' has trailing bytes after mip payloads", path);
        goto fail;
    }

    texture->file_data = file_data;
    texture->mips = mips;
    texture->mip_count = mip_count;
    texture->width = width;
    texture->height = height;
    texture->format = format;
    return true;

fail:
    SDL_free(mips);
    SDL_free(file_data);
    return false;
}

bool ForgeGpuValidateProcessedTextureSidecar(const char *image_path, Uint32 *out_width, Uint32 *out_height)
{
    char *meta_path;
    cJSON *root;
    const cJSON *width_json;
    const cJSON *height_json;
    const cJSON *format_json;
    const cJSON *mips_json;
    int width;
    int height;

    if (out_width) {
        *out_width = 0;
    }
    if (out_height) {
        *out_height = 0;
    }
    if (!image_path) {
        SDL_SetError("forge processed assets: texture image path is missing");
        return false;
    }

    meta_path = meta_path_for_image(image_path);
    if (!meta_path) {
        SDL_SetError("forge processed assets: texture meta path allocation failed");
        return false;
    }

    root = load_json_file(meta_path);
    SDL_free(meta_path);
    if (!root) {
        return false;
    }

    width_json = cJSON_GetObjectItemCaseSensitive(root, "output_width");
    height_json = cJSON_GetObjectItemCaseSensitive(root, "output_height");
    if (!width_json) {
        width_json = cJSON_GetObjectItemCaseSensitive(root, "width");
    }
    if (!height_json) {
        height_json = cJSON_GetObjectItemCaseSensitive(root, "height");
    }
    format_json = cJSON_GetObjectItemCaseSensitive(root, "format");

    if (!cJSON_IsNumber(width_json) || !cJSON_IsNumber(height_json) ||
        width_json->valueint <= 0 || height_json->valueint <= 0) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid dimensions", image_path);
        goto fail;
    }
    width = width_json->valueint;
    height = height_json->valueint;
    if (format_json && (!cJSON_IsString(format_json) || SDL_strcmp(format_json->valuestring, "rgba8") != 0)) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has unsupported format", image_path);
        goto fail;
    }

    mips_json = cJSON_GetObjectItemCaseSensitive(root, "mips");
    if (!mips_json) {
        mips_json = cJSON_GetObjectItemCaseSensitive(root, "mip_levels");
    }
    if (mips_json) {
        const cJSON *mip0;
        const cJSON *mip_width;
        const cJSON *mip_height;

        if (!cJSON_IsArray(mips_json) || cJSON_GetArraySize(mips_json) <= 0) {
            SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid mip list", image_path);
            goto fail;
        }
        mip0 = cJSON_GetArrayItem(mips_json, 0);
        mip_width = cJSON_GetObjectItemCaseSensitive(mip0, "width");
        mip_height = cJSON_GetObjectItemCaseSensitive(mip0, "height");
        if (!cJSON_IsNumber(mip_width) || !cJSON_IsNumber(mip_height) ||
            mip_width->valueint != width || mip_height->valueint != height) {
            SDL_SetError("forge processed assets: texture sidecar for '%s' mip 0 dimensions do not match", image_path);
            goto fail;
        }
    }

    if (out_width) {
        *out_width = (Uint32)width;
    }
    if (out_height) {
        *out_height = (Uint32)height;
    }
    cJSON_Delete(root);
    return true;

fail:
    cJSON_Delete(root);
    return false;
}

bool ForgeGpuLoadProcessedTextureCompressionSidecar(
    const char *image_path,
    ForgeGpuProcessedTextureCompressionInfo *info)
{
    char *meta_path;
    cJSON *root;
    const cJSON *compression;
    const cJSON *ftex_format;
    const cJSON *ftex_file;
    const cJSON *normal_map;
    const cJSON *mips_json;
    bool ok = false;

    if (!image_path || !info) {
        SDL_SetError("forge processed assets: compression sidecar arguments are invalid");
        return false;
    }
    SDL_zero(*info);

    meta_path = meta_path_for_image(image_path);
    if (!meta_path) {
        SDL_SetError("forge processed assets: texture meta path allocation failed");
        return false;
    }

    root = load_json_file(meta_path);
    SDL_free(meta_path);
    if (!root) {
        return false;
    }

    compression = cJSON_GetObjectItemCaseSensitive(root, "compression");
    if (!cJSON_IsObject(compression)) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has no compression block", image_path);
        goto done;
    }

    if (!read_json_u32(root, "output_width", true, &info->output_width) ||
        !read_json_u32(root, "output_height", true, &info->output_height)) {
        goto done;
    }
    if (info->output_width == 0 || info->output_height == 0) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid dimensions", image_path);
        goto done;
    }

    mips_json = cJSON_GetObjectItemCaseSensitive(root, "mip_levels");
    if (!cJSON_IsArray(mips_json) || cJSON_GetArraySize(mips_json) <= 0) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid mip list", image_path);
        goto done;
    }
    info->mip_count = (Uint32)cJSON_GetArraySize(mips_json);
    if (info->mip_count > FORGE_GPU_PROCESSED_FTEX_MAX_MIP_LEVELS) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has too many mips", image_path);
        goto done;
    }
    for (Uint32 i = 0; i < info->mip_count; i += 1) {
        const cJSON *mip = cJSON_GetArrayItem(mips_json, (int)i);
        Uint32 mip_width;
        Uint32 mip_height;
        Uint32 expected_width = info->output_width >> i;
        Uint32 expected_height = info->output_height >> i;

        if (expected_width == 0) {
            expected_width = 1;
        }
        if (expected_height == 0) {
            expected_height = 1;
        }
        if (!cJSON_IsObject(mip) ||
            !read_json_u32(mip, "width", true, &mip_width) ||
            !read_json_u32(mip, "height", true, &mip_height) ||
            mip_width != expected_width ||
            mip_height != expected_height) {
            SDL_SetError("forge processed assets: texture sidecar for '%s' mip %u facts did not match", image_path, (unsigned int)i);
            goto done;
        }
    }

    ftex_file = cJSON_GetObjectItemCaseSensitive(compression, "ftex_file");
    if (cJSON_IsString(ftex_file) && ftex_file->valuestring) {
        if (SDL_strlen(ftex_file->valuestring) >= sizeof(info->ftex_file)) {
            SDL_SetError("forge processed assets: ftex_file for '%s' is too long", image_path);
            goto done;
        }
        SDL_strlcpy(info->ftex_file, ftex_file->valuestring, sizeof(info->ftex_file));
    } else {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has no .ftex file", image_path);
        goto done;
    }
    if (info->ftex_file[0] == '\0') {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has an empty .ftex file", image_path);
        goto done;
    }
    if (!validate_relative_asset_path("ftex_file", info->ftex_file)) {
        goto done;
    }

    ftex_format = cJSON_GetObjectItemCaseSensitive(compression, "ftex_format");
    if (!cJSON_IsString(ftex_format) || !ftex_format->valuestring ||
        !processed_ftex_format_from_string(ftex_format->valuestring, &info->ftex_format)) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid .ftex format", image_path);
        goto done;
    }
    if (!read_json_u64(compression, "ftex_bytes", true, &info->ftex_bytes) || info->ftex_bytes == 0) {
        SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid .ftex byte count", image_path);
        goto done;
    }

    normal_map = cJSON_GetObjectItemCaseSensitive(compression, "normal_map");
    if (normal_map) {
        if (!cJSON_IsBool(normal_map)) {
            SDL_SetError("forge processed assets: texture sidecar for '%s' has invalid normal_map flag", image_path);
            goto done;
        }
        info->normal_map = cJSON_IsTrue(normal_map);
    }

    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}

Uint64 ForgeGpuEstimateProcessedRgba8MipBytes(Uint32 width, Uint32 height)
{
    Uint64 total = 0;

    while (width >= 1 && height >= 1) {
        total += (Uint64)width * (Uint64)height * 4u;
        if (width == 1 && height == 1) {
            break;
        }
        if (width > 1) {
            width >>= 1;
        }
        if (height > 1) {
            height >>= 1;
        }
    }
    return total;
}

static bool join_asset_path(const char *asset_root, const char *relative, char *path, size_t path_size)
{
    int result;

    if (!asset_root || !relative || !path || path_size == 0) {
        SDL_SetError("forge processed assets: asset path arguments are invalid");
        return false;
    }
    result = SDL_snprintf(path, path_size, "%s/%s", asset_root, relative);
    if (result <= 0 || (size_t)result >= path_size) {
        SDL_SetError("forge processed assets: asset path is too long");
        return false;
    }
    return true;
}

static bool join_pref_path(const char *pref_path, const char *leaf, char *path, size_t path_size)
{
    int result;

    if (!pref_path || !leaf || !path || path_size == 0) {
        SDL_SetError("forge processed assets: self-test path arguments are invalid");
        return false;
    }
    result = SDL_snprintf(path, path_size, "%s%s", pref_path, leaf);
    if (result <= 0 || (size_t)result >= path_size) {
        SDL_SetError("forge processed assets: self-test path is too long");
        return false;
    }
    return true;
}

static bool remove_path_preserving_error(const char *path)
{
    char *saved_error;
    char *remove_error = NULL;
    bool removed;

    if (!path || path[0] == '\0') {
        return true;
    }
    saved_error = SDL_strdup(SDL_GetError());
    removed = SDL_RemovePath(path);
    if (!removed) {
        const char *error = SDL_GetError();
        remove_error = error && error[0] ? SDL_strdup(error) : NULL;
    }
    if (saved_error && saved_error[0]) {
        SDL_SetError("%s", saved_error);
    } else {
        SDL_ClearError();
    }
    SDL_free(saved_error);

    if (!removed) {
        SDL_SetError(
            "forge processed assets: failed to remove self-test file '%s': %s",
            path,
            remove_error ? remove_error : "unknown error");
        SDL_free(remove_error);
    }
    return removed;
}

static bool save_self_test_file(const char *path, const void *data, size_t size)
{
    if (!SDL_SaveFile(path, data, size)) {
        const char *error = SDL_GetError();
        char *error_copy = error && error[0] ? SDL_strdup(error) : NULL;

        SDL_SetError(
            "forge processed assets: failed to write self-test file '%s': %s",
            path,
            error_copy ? error_copy : "unknown error");
        SDL_free(error_copy);
        return false;
    }
    return true;
}

static void write_u32_le(Uint8 **cursor, Uint32 value)
{
    value = SDL_Swap32LE(value);
    SDL_memcpy(*cursor, &value, sizeof(value));
    *cursor += sizeof(value);
}

static void write_i32_le(Uint8 **cursor, Sint32 value)
{
    Uint32 bits;

    SDL_memcpy(&bits, &value, sizeof(bits));
    write_u32_le(cursor, bits);
}

static void write_f32_le(Uint8 **cursor, float value)
{
    Uint32 bits;

    SDL_memcpy(&bits, &value, sizeof(bits));
    write_u32_le(cursor, bits);
}

static void write_self_test_fscene_node(
    Uint8 **cursor,
    const char *name,
    Sint32 parent,
    Sint32 mesh_index,
    Uint32 first_child,
    Uint32 child_count,
    float tx,
    float ty,
    float tz)
{
    Uint8 name_bytes[64];
    float local[16];

    SDL_zeroa(name_bytes);
    SDL_strlcpy((char *)name_bytes, name, sizeof(name_bytes));
    SDL_memcpy(*cursor, name_bytes, sizeof(name_bytes));
    *cursor += sizeof(name_bytes);

    write_i32_le(cursor, parent);
    write_i32_le(cursor, mesh_index);
    write_i32_le(cursor, -1);
    write_u32_le(cursor, first_child);
    write_u32_le(cursor, child_count);
    write_u32_le(cursor, 1);
    write_f32_le(cursor, tx);
    write_f32_le(cursor, ty);
    write_f32_le(cursor, tz);
    write_f32_le(cursor, 0.0f);
    write_f32_le(cursor, 0.0f);
    write_f32_le(cursor, 0.0f);
    write_f32_le(cursor, 1.0f);
    write_f32_le(cursor, 1.0f);
    write_f32_le(cursor, 1.0f);
    write_f32_le(cursor, 1.0f);

    mat4_identity(local);
    local[12] = tx;
    local[13] = ty;
    local[14] = tz;
    for (int i = 0; i < 16; i += 1) {
        write_f32_le(cursor, local[i]);
    }
}

static size_t write_self_test_fscene(Uint8 *data, Sint32 child_parent, Uint32 child_index, Uint32 reserved)
{
    Uint8 *cursor = data;

    SDL_memcpy(cursor, FORGE_GPU_PROCESSED_FSCENE_MAGIC, 4);
    cursor += 4;
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FSCENE_VERSION);
    write_u32_le(&cursor, 2);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, reserved);
    write_u32_le(&cursor, 0);
    write_u32_le(&cursor, 0);
    write_u32_le(&cursor, 1);
    write_self_test_fscene_node(&cursor, "root", -1, -1, 0, 1, 0.0f, 0.0f, 0.0f);
    write_self_test_fscene_node(&cursor, "mesh", child_parent, 0, 1, 0, 1.0f, 2.0f, 3.0f);
    write_u32_le(&cursor, child_index);
    return (size_t)(cursor - data);
}

static bool require_expected_failure_error(const char *label)
{
    const char *error = SDL_GetError();

    if (!error || error[0] == '\0') {
        SDL_SetError("forge processed assets: %s failed without setting SDL error", label);
        return false;
    }
    return true;
}

static size_t write_self_test_fskin(Uint8 *data)
{
    Uint8 *cursor = data;

    SDL_memcpy(cursor, FORGE_GPU_PROCESSED_FSKIN_MAGIC, 4);
    cursor += 4;
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FSKIN_VERSION);
    write_u32_le(&cursor, 1);
    SDL_memset(cursor, 0, FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE);
    SDL_strlcpy((char *)cursor, "skin", FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE);
    cursor += FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE;
    write_u32_le(&cursor, 1);
    write_i32_le(&cursor, 0);
    write_i32_le(&cursor, 0);
    for (int i = 0; i < 16; i += 1) {
        write_f32_le(&cursor, i % 5 == 0 ? 1.0f : 0.0f);
    }
    return (size_t)(cursor - data);
}

static size_t write_self_test_fanim(Uint8 *data, Uint32 interpolation, Uint32 target_path)
{
    Uint8 *cursor = data;

    SDL_memcpy(cursor, FORGE_GPU_PROCESSED_FANIM_MAGIC, 4);
    cursor += 4;
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FANIM_VERSION);
    write_u32_le(&cursor, 1);
    SDL_memset(cursor, 0, FORGE_GPU_PROCESSED_FANIM_NAME_SIZE);
    SDL_strlcpy((char *)cursor, "clip", FORGE_GPU_PROCESSED_FANIM_NAME_SIZE);
    cursor += FORGE_GPU_PROCESSED_FANIM_NAME_SIZE;
    write_f32_le(&cursor, 1.0f);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, 3);
    write_u32_le(&cursor, interpolation);
    write_f32_le(&cursor, 0.0f);
    write_f32_le(&cursor, 1.0f);
    write_f32_le(&cursor, 2.0f);
    write_f32_le(&cursor, 3.0f);
    write_i32_le(&cursor, 0);
    write_u32_le(&cursor, target_path);
    write_u32_le(&cursor, 0);
    return (size_t)(cursor - data);
}

static bool run_rejected_fskin_self_test(
    const char *pref_path,
    const char *name,
    const Uint8 *bytes,
    size_t size,
    const char *label,
    const char *accepted_error)
{
    char path[1024];
    ForgeGpuProcessedSkinSet skins;

    if (!join_pref_path(pref_path, name, path, sizeof(path)) ||
        !save_self_test_file(path, bytes, size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedSkins(path, &skins)) {
        ForgeGpuFreeProcessedSkins(&skins);
        SDL_SetError("%s", accepted_error);
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed = remove_path_preserving_error(path);
    if (!require_expected_failure_error(label)) {
        return false;
    }
    return removed;
}

static bool run_rejected_fanim_self_test(
    const char *pref_path,
    const char *name,
    const Uint8 *bytes,
    size_t size,
    const char *label,
    const char *accepted_error)
{
    char path[1024];
    ForgeGpuProcessedAnimation animation;

    if (!join_pref_path(pref_path, name, path, sizeof(path)) ||
        !save_self_test_file(path, bytes, size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedAnimation(path, &animation)) {
        ForgeGpuFreeProcessedAnimation(&animation);
        SDL_SetError("%s", accepted_error);
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed = remove_path_preserving_error(path);
    if (!require_expected_failure_error(label)) {
        return false;
    }
    return removed;
}

static bool run_processed_scene_reference_self_tests(void)
{
    ForgeGpuProcessedSceneMesh scene_mesh = { 0, 1 };
    ForgeGpuProcessedSubmesh submesh = { 3, 0, 0 };
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;

    SDL_zero(scene);
    SDL_zero(mesh);
    SDL_zero(materials);
    scene.meshes = &scene_mesh;
    scene.mesh_count = 1;
    mesh.submeshes = &submesh;
    mesh.submesh_count = 1;
    mesh.lod_count = 1;
    materials.material_count = 1;

    if (!ForgeGpuValidateProcessedSceneModelReferences(&scene, &mesh, &materials, "self-test valid references")) {
        return false;
    }

    scene_mesh.submesh_count = 2;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneModelReferences(&scene, &mesh, &materials, "self-test invalid submesh range")) {
        SDL_SetError("forge processed assets: invalid scene submesh range was accepted");
        return false;
    }
    if (!require_expected_failure_error("invalid scene submesh range")) {
        return false;
    }

    scene_mesh.submesh_count = 1;
    submesh.material_index = 1;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneModelReferences(&scene, &mesh, &materials, "self-test invalid material index")) {
        SDL_SetError("forge processed assets: invalid mesh material reference was accepted");
        return false;
    }
    return require_expected_failure_error("invalid mesh material reference");
}

static bool run_processed_skin_animation_reference_self_tests(void)
{
    ForgeGpuProcessedSceneNode node;
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedSkin skin;
    ForgeGpuProcessedSkinSet skins;
    ForgeGpuProcessedAnimationChannel channel;
    ForgeGpuProcessedAnimationClip clip;
    ForgeGpuProcessedAnimation animation;
    Sint32 joints[1] = { 0 };
    float inverse_bind[16];

    SDL_zero(node);
    SDL_zero(scene);
    SDL_zero(mesh);
    SDL_zero(skin);
    SDL_zero(skins);
    SDL_zero(channel);
    SDL_zero(clip);
    SDL_zero(animation);

    node.skin_index = 0;
    node.has_trs = true;
    scene.nodes = &node;
    scene.node_count = 1;
    mesh.flags = FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED;
    mesh.vertex_stride = FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS;
    skin.joints = joints;
    skin.inverse_bind_matrices = inverse_bind;
    skin.joint_count = 1;
    skin.skeleton = 0;
    skins.skins = &skin;
    skins.skin_count = 1;

    if (!ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, "self-test valid skin references")) {
        return false;
    }

    node.skin_index = 1;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, "self-test invalid skin index")) {
        SDL_SetError("forge processed assets: invalid scene skin index was accepted");
        return false;
    }
    if (!require_expected_failure_error("invalid scene skin index")) {
        return false;
    }

    node.skin_index = 0;
    mesh.flags = 0;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, "self-test non-skinned mesh")) {
        SDL_SetError("forge processed assets: scene skin reference on non-skinned mesh was accepted");
        return false;
    }
    if (!require_expected_failure_error("scene skin reference on non-skinned mesh")) {
        return false;
    }

    mesh.flags = FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED;
    joints[0] = 1;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, "self-test invalid skin joint")) {
        SDL_SetError("forge processed assets: invalid skin joint reference was accepted");
        return false;
    }
    if (!require_expected_failure_error("invalid skin joint reference")) {
        return false;
    }

    joints[0] = 0;
    skin.skeleton = 1;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, "self-test invalid skin skeleton")) {
        SDL_SetError("forge processed assets: invalid skin skeleton reference was accepted");
        return false;
    }
    if (!require_expected_failure_error("invalid skin skeleton reference")) {
        return false;
    }
    skin.skeleton = 0;

    channel.target_node = 0;
    channel.target_path = FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION;
    channel.sampler_index = 0;
    clip.channels = &channel;
    clip.channel_count = 1;
    animation.clips = &clip;
    animation.clip_count = 1;

    if (!ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, "self-test valid animation references")) {
        return false;
    }

    channel.target_node = 1;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, "self-test invalid animation target")) {
        SDL_SetError("forge processed assets: invalid animation target node was accepted");
        return false;
    }
    if (!require_expected_failure_error("invalid animation target node")) {
        return false;
    }

    channel.target_node = 0;
    node.has_trs = false;
    SDL_ClearError();
    if (ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, "self-test matrix-only animation target")) {
        SDL_SetError("forge processed assets: matrix-only animation target was accepted");
        return false;
    }
    if (!require_expected_failure_error("matrix-only animation target")) {
        return false;
    }

    channel.target_path = FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS;
    if (!ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, "self-test morph animation target")) {
        return false;
    }

    return true;
}

static bool run_processed_scene_self_tests(const char *pref_path)
{
    enum
    {
        SELF_TEST_FSCENE_SIZE =
            FORGE_GPU_PROCESSED_FSCENE_HEADER_SIZE +
            sizeof(Uint32) +
            8 +
            (FORGE_GPU_PROCESSED_FSCENE_NODE_SIZE * 2) +
            sizeof(Uint32)
    };
    Uint8 scene_bytes[SELF_TEST_FSCENE_SIZE + 1];
    char path[1024];
    ForgeGpuProcessedScene scene;
    size_t scene_size;

    scene_size = write_self_test_fscene(scene_bytes, 0, 1, 0);
    if (scene_size != SELF_TEST_FSCENE_SIZE ||
        !join_pref_path(pref_path, "valid.fscene", path, sizeof(path)) ||
        !save_self_test_file(path, scene_bytes, scene_size)) {
        return false;
    }
    if (!ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        (void)remove_path_preserving_error(path);
        return false;
    }
    if (scene.node_count != 2 ||
        scene.mesh_count != 1 ||
        scene.root_count != 1 ||
        scene.child_count != 1 ||
        scene.nodes[1].mesh_index != 0 ||
        scene.nodes[1].world_transform[12] != 1.0f ||
        scene.nodes[1].world_transform[13] != 2.0f ||
        scene.nodes[1].world_transform[14] != 3.0f) {
        ForgeGpuFreeProcessedScene(&scene);
        SDL_SetError("forge processed assets: valid .fscene self-test facts did not match");
        (void)remove_path_preserving_error(path);
        return false;
    }
    ForgeGpuFreeProcessedScene(&scene);
    if (!remove_path_preserving_error(path)) {
        return false;
    }

    scene_size = write_self_test_fscene(scene_bytes, -1, 1, 0);
    if (!join_pref_path(pref_path, "bad-parent.fscene", path, sizeof(path)) ||
        !save_self_test_file(path, scene_bytes, scene_size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        ForgeGpuFreeProcessedScene(&scene);
        SDL_SetError("forge processed assets: parent-mismatched .fscene was accepted");
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed_parent = remove_path_preserving_error(path);
    if (!require_expected_failure_error("parent-mismatched .fscene")) {
        return false;
    }
    if (!removed_parent) {
        return false;
    }

    scene_size = write_self_test_fscene(scene_bytes, 0, 2, 0);
    if (!join_pref_path(pref_path, "bad-child.fscene", path, sizeof(path)) ||
        !save_self_test_file(path, scene_bytes, scene_size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        ForgeGpuFreeProcessedScene(&scene);
        SDL_SetError("forge processed assets: child-out-of-range .fscene was accepted");
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed_child = remove_path_preserving_error(path);
    if (!require_expected_failure_error("child-out-of-range .fscene")) {
        return false;
    }
    if (!removed_child) {
        return false;
    }

    scene_size = write_self_test_fscene(scene_bytes, 0, 1, 1);
    if (!join_pref_path(pref_path, "bad-reserved.fscene", path, sizeof(path)) ||
        !save_self_test_file(path, scene_bytes, scene_size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        ForgeGpuFreeProcessedScene(&scene);
        SDL_SetError("forge processed assets: nonzero-reserved .fscene was accepted");
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed_reserved = remove_path_preserving_error(path);
    if (!require_expected_failure_error("nonzero-reserved .fscene")) {
        return false;
    }
    if (!removed_reserved) {
        return false;
    }

    scene_size = write_self_test_fscene(scene_bytes, 0, 1, 0);
    scene_bytes[scene_size] = 0;
    scene_size += 1;
    if (!join_pref_path(pref_path, "bad-trailing.fscene", path, sizeof(path)) ||
        !save_self_test_file(path, scene_bytes, scene_size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        ForgeGpuFreeProcessedScene(&scene);
        SDL_SetError("forge processed assets: trailing-byte .fscene was accepted");
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed_trailing = remove_path_preserving_error(path);
    if (!require_expected_failure_error("trailing-byte .fscene")) {
        return false;
    }
    return removed_trailing;
}

static void write_self_test_ftex_one_mip(
    Uint8 *bytes,
    size_t size,
    Uint32 reserved0,
    Uint32 mip_offset,
    Uint32 mip_size)
{
    Uint8 *cursor = bytes;

    SDL_memset(bytes, 0, size);
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_MAGIC);
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_VERSION);
    write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_BC7_SRGB);
    write_u32_le(&cursor, 4);
    write_u32_le(&cursor, 4);
    write_u32_le(&cursor, 1);
    write_u32_le(&cursor, reserved0);
    write_u32_le(&cursor, 0);
    write_u32_le(&cursor, mip_offset);
    write_u32_le(&cursor, mip_size);
    write_u32_le(&cursor, 4);
    write_u32_le(&cursor, 4);

    if ((size_t)mip_offset < size) {
        bytes[mip_offset] = 0x40;
    }
}

static bool run_rejected_ftex_self_test(
    const char *pref_path,
    const char *name,
    const Uint8 *bytes,
    size_t size,
    const char *label,
    const char *accepted_error)
{
    char path[1024];
    ForgeGpuProcessedCompressedTexture texture;

    if (!join_pref_path(pref_path, name, path, sizeof(path)) ||
        !save_self_test_file(path, bytes, size)) {
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedFtexV1(path, &texture)) {
        ForgeGpuFreeProcessedCompressedTexture(&texture);
        SDL_SetError("%s", accepted_error);
        (void)remove_path_preserving_error(path);
        return false;
    }
    const bool removed = remove_path_preserving_error(path);
    if (!require_expected_failure_error(label)) {
        return false;
    }
    return removed;
}

static bool run_negative_processed_asset_self_tests(void)
{
    enum
    {
        SELF_TEST_FSKIN_SIZE =
            FORGE_GPU_PROCESSED_FSKIN_HEADER_SIZE +
            FORGE_GPU_PROCESSED_FSKIN_NAME_SIZE +
            8u +
            sizeof(Sint32) +
            16u * sizeof(float),
        SELF_TEST_FANIM_SIZE =
            FORGE_GPU_PROCESSED_FANIM_HEADER_SIZE +
            FORGE_GPU_PROCESSED_FANIM_NAME_SIZE +
            12u +
            12u +
            sizeof(float) +
            3u * sizeof(float) +
            12u
    };
    Uint8 bad_mesh[FORGE_GPU_PROCESSED_FMESH_HEADER_SIZE];
    Uint8 bad_skin[FORGE_GPU_PROCESSED_FSKIN_HEADER_SIZE];
    Uint8 valid_skin[SELF_TEST_FSKIN_SIZE + 1u];
    Uint8 bad_anim[FORGE_GPU_PROCESSED_FANIM_HEADER_SIZE];
    Uint8 valid_anim[SELF_TEST_FANIM_SIZE + 1u];
    Uint8 bad_anim_sampler[SELF_TEST_FANIM_SIZE];
    Uint8 bad_anim_target_path[SELF_TEST_FANIM_SIZE];
    Uint8 bad_ftex[FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE];
    Uint8 bad_ftex_reserved[
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE +
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK];
    Uint8 bad_ftex_payload[
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE +
        (FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK - 1u)];
    Uint8 bad_ftex_offset[
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE +
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK + 1u];
    Uint8 bad_ftex_trailing[
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE +
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK + 1u];
    const char bad_materials[] =
        "{ \"version\": 1, \"materials\": [ { \"base_color_factor\": [1, 1, 1] } ] }\n";
    const char bad_material_path[] =
        "{ \"version\": 1, \"materials\": [ { \"base_color_texture\": \"../secret.png\" } ] }\n";
    const char bad_material_trailing[] =
        "{ \"version\": 1, \"materials\": [] } trailing\n";
    const char mismatched_texture_meta[] =
        "{ \"width\": 4, \"height\": 4, \"format\": \"rgba8\","
        " \"mips\": [ { \"width\": 2, \"height\": 4 } ] }\n";
    char overlong_material_path[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE + 2];
    char bad_material_overlong_path[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE + 256];
    char *pref_path;
    char path[1024];
    char meta_path[1024];
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    Uint32 width = 0;
    Uint32 height = 0;
    size_t skin_size;
    size_t anim_size;

    pref_path = SDL_GetPrefPath("forge-gpu-demo", "processed-asset-self-test");
    if (!pref_path) {
        return false;
    }

    SDL_memset(bad_mesh, 0, sizeof(bad_mesh));
    SDL_memcpy(bad_mesh, "BAD!", 4);
    if (!join_pref_path(pref_path, "bad.fmesh", path, sizeof(path)) ||
        !save_self_test_file(path, bad_mesh, sizeof(bad_mesh))) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedMesh(path, &mesh)) {
        ForgeGpuFreeProcessedMesh(&mesh);
        SDL_SetError("forge processed assets: malformed .fmesh was accepted");
        (void)remove_path_preserving_error(path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_mesh = remove_path_preserving_error(path);
    if (!require_expected_failure_error("malformed .fmesh")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_mesh) {
        SDL_free(pref_path);
        return false;
    }

    SDL_memset(bad_skin, 0, sizeof(bad_skin));
    SDL_memcpy(bad_skin, "BAD!", 4);
    if (!run_rejected_fskin_self_test(
            pref_path,
            "bad.fskin",
            bad_skin,
            sizeof(bad_skin),
            "malformed .fskin",
            "forge processed assets: malformed .fskin was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    skin_size = write_self_test_fskin(valid_skin);
    if (skin_size != SELF_TEST_FSKIN_SIZE) {
        SDL_SetError("forge processed assets: .fskin self-test writer produced unexpected size");
        SDL_free(pref_path);
        return false;
    }
    if (!run_rejected_fskin_self_test(
            pref_path,
            "bad-truncated.fskin",
            valid_skin,
            skin_size - 1u,
            "truncated .fskin",
            "forge processed assets: truncated .fskin was accepted")) {
        SDL_free(pref_path);
        return false;
    }
    valid_skin[skin_size] = 0;
    if (!run_rejected_fskin_self_test(
            pref_path,
            "bad-trailing.fskin",
            valid_skin,
            skin_size + 1u,
            "trailing-byte .fskin",
            "forge processed assets: trailing-byte .fskin was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    SDL_memset(bad_anim, 0, sizeof(bad_anim));
    SDL_memcpy(bad_anim, "BAD!", 4);
    if (!run_rejected_fanim_self_test(
            pref_path,
            "bad.fanim",
            bad_anim,
            sizeof(bad_anim),
            "malformed .fanim",
            "forge processed assets: malformed .fanim was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    anim_size = write_self_test_fanim(
        valid_anim,
        FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_LINEAR,
        FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION);
    if (anim_size != SELF_TEST_FANIM_SIZE) {
        SDL_SetError("forge processed assets: .fanim self-test writer produced unexpected size");
        SDL_free(pref_path);
        return false;
    }
    if (!run_rejected_fanim_self_test(
            pref_path,
            "bad-truncated.fanim",
            valid_anim,
            anim_size - 1u,
            "truncated .fanim",
            "forge processed assets: truncated .fanim was accepted")) {
        SDL_free(pref_path);
        return false;
    }
    valid_anim[anim_size] = 0;
    if (!run_rejected_fanim_self_test(
            pref_path,
            "bad-trailing.fanim",
            valid_anim,
            anim_size + 1u,
            "trailing-byte .fanim",
            "forge processed assets: trailing-byte .fanim was accepted")) {
        SDL_free(pref_path);
        return false;
    }
    (void)write_self_test_fanim(
        bad_anim_sampler,
        99u,
        FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION);
    if (!run_rejected_fanim_self_test(
            pref_path,
            "bad-interpolation.fanim",
            bad_anim_sampler,
            sizeof(bad_anim_sampler),
            "invalid .fanim interpolation",
            "forge processed assets: invalid .fanim interpolation was accepted")) {
        SDL_free(pref_path);
        return false;
    }
    (void)write_self_test_fanim(
        bad_anim_target_path,
        FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_LINEAR,
        99u);
    if (!run_rejected_fanim_self_test(
            pref_path,
            "bad-target-path.fanim",
            bad_anim_target_path,
            sizeof(bad_anim_target_path),
            "invalid .fanim target path",
            "forge processed assets: invalid .fanim target path was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    SDL_memset(bad_ftex, 0, sizeof(bad_ftex));
    {
        Uint8 *cursor = bad_ftex;
        write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_MAGIC);
        write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_VERSION);
        write_u32_le(&cursor, FORGE_GPU_PROCESSED_FTEX_BC7_SRGB);
        write_u32_le(&cursor, 4);
        write_u32_le(&cursor, 4);
        write_u32_le(&cursor, 0);
        write_u32_le(&cursor, 0);
        write_u32_le(&cursor, 0);
    }
    if (!run_rejected_ftex_self_test(
            pref_path,
            "bad.ftex",
            bad_ftex,
            sizeof(bad_ftex),
            "malformed .ftex",
            "forge processed assets: malformed .ftex was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    write_self_test_ftex_one_mip(
        bad_ftex_reserved,
        sizeof(bad_ftex_reserved),
        1,
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE,
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK);
    if (!run_rejected_ftex_self_test(
            pref_path,
            "bad-reserved.ftex",
            bad_ftex_reserved,
            sizeof(bad_ftex_reserved),
            ".ftex reserved fields",
            "forge processed assets: .ftex with nonzero reserved fields was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    write_self_test_ftex_one_mip(
        bad_ftex_payload,
        sizeof(bad_ftex_payload),
        0,
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE,
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK - 1u);
    if (!run_rejected_ftex_self_test(
            pref_path,
            "bad-payload.ftex",
            bad_ftex_payload,
            sizeof(bad_ftex_payload),
            ".ftex truncated payload",
            "forge processed assets: truncated .ftex mip payload was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    write_self_test_ftex_one_mip(
        bad_ftex_offset,
        sizeof(bad_ftex_offset),
        0,
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE + 1u,
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK);
    if (!run_rejected_ftex_self_test(
            pref_path,
            "bad-offset.ftex",
            bad_ftex_offset,
            sizeof(bad_ftex_offset),
            ".ftex non-contiguous mip payload",
            "forge processed assets: non-contiguous .ftex mip payload was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    write_self_test_ftex_one_mip(
        bad_ftex_trailing,
        sizeof(bad_ftex_trailing),
        0,
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE + FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE,
        FORGE_GPU_PROCESSED_FTEX_BC_BYTES_PER_BLOCK);
    if (!run_rejected_ftex_self_test(
            pref_path,
            "bad-trailing.ftex",
            bad_ftex_trailing,
            sizeof(bad_ftex_trailing),
            ".ftex trailing byte",
            "forge processed assets: trailing-byte .ftex was accepted")) {
        SDL_free(pref_path);
        return false;
    }

    if (!join_pref_path(pref_path, "bad.fmat", path, sizeof(path)) ||
        !save_self_test_file(path, bad_materials, sizeof(bad_materials) - 1)) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedMaterials(path, &materials)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        SDL_SetError("forge processed assets: malformed .fmat was accepted");
        (void)remove_path_preserving_error(path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_materials = remove_path_preserving_error(path);
    if (!require_expected_failure_error("malformed .fmat")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_materials) {
        SDL_free(pref_path);
        return false;
    }

    if (!join_pref_path(pref_path, "bad-material-trailing.fmat", path, sizeof(path)) ||
        !save_self_test_file(path, bad_material_trailing, sizeof(bad_material_trailing) - 1)) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedMaterials(path, &materials)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        SDL_SetError("forge processed assets: trailing-data .fmat was accepted");
        (void)remove_path_preserving_error(path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_material_trailing = remove_path_preserving_error(path);
    if (!require_expected_failure_error("trailing-data .fmat")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_material_trailing) {
        SDL_free(pref_path);
        return false;
    }

    if (!join_pref_path(pref_path, "bad-material-path.fmat", path, sizeof(path)) ||
        !save_self_test_file(path, bad_material_path, sizeof(bad_material_path) - 1)) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedMaterials(path, &materials)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        SDL_SetError("forge processed assets: path-traversal .fmat was accepted");
        (void)remove_path_preserving_error(path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_material_path = remove_path_preserving_error(path);
    if (!require_expected_failure_error("path-traversal .fmat")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_material_path) {
        SDL_free(pref_path);
        return false;
    }

    SDL_memset(overlong_material_path, 'a', sizeof(overlong_material_path) - 1);
    overlong_material_path[sizeof(overlong_material_path) - 1] = '\0';
    const int overlong_material_json_len = SDL_snprintf(
        bad_material_overlong_path,
        sizeof(bad_material_overlong_path),
        "{ \"version\": 1, \"materials\": [ { \"base_color_texture\": \"%s\" } ] }\n",
        overlong_material_path);
    if (overlong_material_json_len < 0 ||
        overlong_material_json_len >= (int)sizeof(bad_material_overlong_path)) {
        SDL_SetError("forge processed assets: overlong path self-test JSON formatting failed");
        SDL_free(pref_path);
        return false;
    }
    if (!join_pref_path(pref_path, "bad-material-overlong-path.fmat", path, sizeof(path)) ||
        !save_self_test_file(path, bad_material_overlong_path, (size_t)overlong_material_json_len)) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuLoadProcessedMaterials(path, &materials)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        SDL_SetError("forge processed assets: overlong-path .fmat was accepted");
        (void)remove_path_preserving_error(path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_material_overlong_path = remove_path_preserving_error(path);
    if (!require_expected_failure_error("overlong-path .fmat")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_material_overlong_path) {
        SDL_free(pref_path);
        return false;
    }

    if (!run_processed_scene_self_tests(pref_path)) {
        SDL_free(pref_path);
        return false;
    }

    if (!join_pref_path(pref_path, "bad-texture.png", path, sizeof(path)) ||
        !join_pref_path(pref_path, "bad-texture.meta.json", meta_path, sizeof(meta_path)) ||
        !save_self_test_file(meta_path, mismatched_texture_meta, sizeof(mismatched_texture_meta) - 1)) {
        SDL_free(pref_path);
        return false;
    }
    SDL_ClearError();
    if (ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
        SDL_SetError("forge processed assets: mismatched texture sidecar was accepted");
        (void)remove_path_preserving_error(meta_path);
        SDL_free(pref_path);
        return false;
    }
    const bool removed_texture_meta = remove_path_preserving_error(meta_path);
    if (!require_expected_failure_error("mismatched texture sidecar")) {
        SDL_free(pref_path);
        return false;
    }
    if (!removed_texture_meta) {
        SDL_free(pref_path);
        return false;
    }

    const bool removed_pref_dir = remove_path_preserving_error(pref_path);
    SDL_free(pref_path);
    return removed_pref_dir;
}

typedef struct Lesson41ProcessedTextureFact
{
    const char *relative_path;
    Uint32 width;
    Uint32 height;
} Lesson41ProcessedTextureFact;

static bool build_lesson41_processed_path(
    const char *directory,
    const char *stem,
    const char *extension,
    char *path,
    size_t path_size)
{
    const int result = SDL_snprintf(
        path,
        path_size,
        "processed/41-scene-model-loading/%s/%s.%s",
        directory,
        stem,
        extension);

    if (result < 0 || (size_t)result >= path_size) {
        SDL_SetError("forge processed assets: lesson 41 fixture path is too long");
        return false;
    }
    return true;
}

static Uint32 count_processed_scene_mesh_nodes(const ForgeGpuProcessedScene *scene)
{
    Uint32 count = 0;

    for (Uint32 i = 0; i < scene->node_count; i += 1) {
        if (scene->nodes[i].mesh_index >= 0) {
            count += 1;
        }
    }

    return count;
}

static bool validate_lesson41_processed_materials(
    const char *stem,
    const ForgeGpuProcessedMaterialSet *materials,
    Uint32 expected_materials,
    Uint32 expected_base_color_textures,
    Uint32 expected_metallic_roughness_textures,
    const char *expected_first_material_name)
{
    Uint32 base_color_textures = 0;
    Uint32 metallic_roughness_textures = 0;

    if (materials->material_count != expected_materials) {
        SDL_SetError(
            "forge processed assets: lesson 41 %s expected %u materials, got %u",
            stem,
            (unsigned int)expected_materials,
            (unsigned int)materials->material_count);
        return false;
    }

    if (materials->material_count == 0 ||
        SDL_strcmp(materials->materials[0].name, expected_first_material_name) != 0) {
        SDL_SetError("forge processed assets: lesson 41 %s first material did not match", stem);
        return false;
    }

    for (Uint32 i = 0; i < materials->material_count; i += 1) {
        const ForgeGpuProcessedMaterial *material = &materials->materials[i];

        if (material->base_color_texture[0] != '\0') {
            base_color_textures += 1;
        }
        if (material->metallic_roughness_texture[0] != '\0') {
            metallic_roughness_textures += 1;
        }
        if (material->roughness_texture[0] != '\0' ||
            material->metallic_texture[0] != '\0' ||
            material->normal_texture[0] != '\0' ||
            material->occlusion_texture[0] != '\0' ||
            material->emissive_texture[0] != '\0' ||
            material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE) {
            SDL_SetError("forge processed assets: lesson 41 %s material facts did not match", stem);
            return false;
        }
    }

    if (base_color_textures != expected_base_color_textures ||
        metallic_roughness_textures != expected_metallic_roughness_textures) {
        SDL_SetError("forge processed assets: lesson 41 %s texture-slot facts did not match", stem);
        return false;
    }

    return true;
}

static bool validate_lesson41_processed_fixture(
    const char *asset_root,
    const char *directory,
    const char *stem,
    Uint32 expected_nodes,
    Uint32 expected_scene_meshes,
    Uint32 expected_roots,
    Uint32 expected_children,
    Uint32 expected_mesh_nodes,
    Uint32 expected_materials,
    Uint32 expected_vertices,
    Uint32 expected_lods,
    Uint32 expected_submeshes,
    Uint32 expected_total_indices,
    Uint32 expected_base_color_textures,
    Uint32 expected_metallic_roughness_textures,
    const char *expected_first_material_name,
    const Lesson41ProcessedTextureFact *textures,
    Uint32 texture_count)
{
    char relative_path[256];
    char path[1024];
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    Uint32 width = 0;
    Uint32 height = 0;

    if (!build_lesson41_processed_path(directory, stem, "fscene", relative_path, sizeof(relative_path)) ||
        !join_asset_path(asset_root, relative_path, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        return false;
    }
    if (scene.node_count != expected_nodes ||
        scene.mesh_count != expected_scene_meshes ||
        scene.root_count != expected_roots ||
        scene.child_count != expected_children ||
        count_processed_scene_mesh_nodes(&scene) != expected_mesh_nodes) {
        SDL_SetError("forge processed assets: lesson 41 %s scene facts did not match", stem);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    ForgeGpuFreeProcessedScene(&scene);

    if (!build_lesson41_processed_path(directory, stem, "fmesh", relative_path, sizeof(relative_path)) ||
        !join_asset_path(asset_root, relative_path, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        return false;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&mesh) ||
        mesh.vertex_count != expected_vertices ||
        mesh.lod_count != expected_lods ||
        mesh.submesh_count != expected_submeshes ||
        mesh.total_index_count != expected_total_indices) {
        SDL_SetError("forge processed assets: lesson 41 %s mesh facts did not match", stem);
        ForgeGpuFreeProcessedMesh(&mesh);
        return false;
    }
    ForgeGpuFreeProcessedMesh(&mesh);

    if (!build_lesson41_processed_path(directory, stem, "fmat", relative_path, sizeof(relative_path)) ||
        !join_asset_path(asset_root, relative_path, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &materials)) {
        return false;
    }
    if (!validate_lesson41_processed_materials(
            stem,
            &materials,
            expected_materials,
            expected_base_color_textures,
            expected_metallic_roughness_textures,
            expected_first_material_name)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        return false;
    }
    ForgeGpuFreeProcessedMaterials(&materials);

    for (Uint32 i = 0; i < texture_count; i += 1) {
        if (!join_asset_path(asset_root, textures[i].relative_path, path, sizeof(path)) ||
            !ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
            return false;
        }
        if (width != textures[i].width || height != textures[i].height) {
            SDL_SetError("forge processed assets: lesson 41 %s texture sidecar facts did not match", stem);
            return false;
        }
    }

    return true;
}

static bool run_lesson41_processed_fixture_self_test(const char *asset_root)
{
    static const Lesson41ProcessedTextureFact truck_textures[] = {
        { "processed/41-scene-model-loading/CesiumMilkTruck/CesiumMilkTruck.png", 2048u, 2048u },
    };
    static const Lesson41ProcessedTextureFact suzanne_textures[] = {
        { "processed/41-scene-model-loading/Suzanne/Suzanne_BaseColor.png", 1024u, 1024u },
        { "processed/41-scene-model-loading/Suzanne/Suzanne_MetallicRoughness.png", 1024u, 1024u },
    };
    static const Lesson41ProcessedTextureFact duck_textures[] = {
        { "processed/41-scene-model-loading/Duck/DuckCM.png", 512u, 512u },
    };

    return validate_lesson41_processed_fixture(
               asset_root,
               "CesiumMilkTruck",
               "CesiumMilkTruck",
               6u, 2u, 1u, 5u, 3u,
               4u, 3916u, 3u, 4u, 18018u,
               2u, 0u, "wheels",
               truck_textures, SDL_arraysize(truck_textures)) &&
        validate_lesson41_processed_fixture(
               asset_root,
               "Suzanne",
               "Suzanne",
               1u, 1u, 1u, 0u, 1u,
               1u, 11808u, 3u, 1u, 35424u,
               1u, 1u, "Suzanne",
               suzanne_textures, SDL_arraysize(suzanne_textures)) &&
        validate_lesson41_processed_fixture(
               asset_root,
               "Duck",
               "Duck",
               3u, 1u, 1u, 2u, 1u,
               1u, 2399u, 3u, 1u, 22110u,
               1u, 0u, "blinn3-fx",
               duck_textures, SDL_arraysize(duck_textures));
}

typedef struct Lesson42TextureExpectation
{
    const char *source_path;
    Uint32 ftex_format;
    bool normal_map;
} Lesson42TextureExpectation;

static const Lesson42TextureExpectation lesson42_texture_expectations[] = {
    { "Bishop_black_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Bishop_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Bishop_black_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Bishop_white_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Bishop_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Bishop_white_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Castle_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Castle_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Castle_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Castle_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Chessboard_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Chessboard_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Chessboard_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "King_black_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "King_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "King_black_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "King_white_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "King_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "King_white_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Knight_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Knight_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Knight_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Knight_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Pawn_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Pawn_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Pawn_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Pawn_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Queen_black_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Queen_black_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Queen_black_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true },
    { "Queen_white_ORM.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_UNORM, false },
    { "Queen_white_base_color.jpg", FORGE_GPU_PROCESSED_FTEX_BC7_SRGB, false },
    { "Queen_white_normal.jpg", FORGE_GPU_PROCESSED_FTEX_BC5_UNORM, true }
};

static bool build_lesson42_processed_path(const char *asset_root, const char *file, char *path, size_t path_size)
{
    char relative_path[512];
    const int result = SDL_snprintf(
        relative_path,
        sizeof(relative_path),
        "processed/42-pipeline-texture-compression/ABeautifulGame/%s",
        file);

    if (result <= 0 || (size_t)result >= sizeof(relative_path)) {
        SDL_SetError("forge processed assets: lesson 42 fixture path is too long");
        return false;
    }
    return join_asset_path(asset_root, relative_path, path, path_size);
}

static bool mark_lesson42_material_texture_reference(
    const char *slot_name,
    const char *path,
    Uint8 used[SDL_arraysize(lesson42_texture_expectations)])
{
    if (path[0] == '\0') {
        return true;
    }
    for (Uint32 i = 0; i < SDL_arraysize(lesson42_texture_expectations); i += 1) {
        if (SDL_strcmp(path, lesson42_texture_expectations[i].source_path) == 0) {
            used[i] = 1;
            return true;
        }
    }
    SDL_SetError("forge processed assets: lesson 42 material references unexpected %s texture '%s'", slot_name, path);
    return false;
}

static bool validate_lesson42_processed_materials(const ForgeGpuProcessedMaterialSet *materials)
{
    Uint32 base_color_textures = 0;
    Uint32 metallic_roughness_textures = 0;
    Uint32 normal_textures = 0;
    Uint32 occlusion_textures = 0;
    Uint32 blend_materials = 0;
    Uint8 used_texture_references[SDL_arraysize(lesson42_texture_expectations)];

    if (!materials || materials->material_count != 15) {
        SDL_SetError(
            "forge processed assets: lesson 42 expected 15 materials, got %u",
            materials ? (unsigned int)materials->material_count : 0u);
        return false;
    }
    SDL_zeroa(used_texture_references);

    for (Uint32 i = 0; i < materials->material_count; i += 1) {
        const ForgeGpuProcessedMaterial *material = &materials->materials[i];

        if (!mark_lesson42_material_texture_reference("base-color", material->base_color_texture, used_texture_references) ||
            !mark_lesson42_material_texture_reference("metallic-roughness", material->metallic_roughness_texture, used_texture_references) ||
            !mark_lesson42_material_texture_reference("normal", material->normal_texture, used_texture_references) ||
            !mark_lesson42_material_texture_reference("occlusion", material->occlusion_texture, used_texture_references)) {
            return false;
        }

        if (material->base_color_texture[0] != '\0') {
            base_color_textures += 1;
        }
        if (material->metallic_roughness_texture[0] != '\0') {
            metallic_roughness_textures += 1;
        }
        if (material->normal_texture[0] != '\0') {
            normal_textures += 1;
        }
        if (material->occlusion_texture[0] != '\0') {
            occlusion_textures += 1;
        }
        if (material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND) {
            blend_materials += 1;
        } else if (material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE) {
            SDL_SetError("forge processed assets: lesson 42 has an unexpected alpha mode");
            return false;
        }
        if (material->roughness_texture[0] != '\0' ||
            material->metallic_texture[0] != '\0' ||
            material->emissive_texture[0] != '\0') {
            SDL_SetError("forge processed assets: lesson 42 material texture facts did not match");
            return false;
        }
    }

    if (base_color_textures != 13 ||
        metallic_roughness_textures != 15 ||
        normal_textures != 15 ||
        occlusion_textures != 13 ||
        blend_materials != 2) {
        SDL_SetError("forge processed assets: lesson 42 material slot counts did not match");
        return false;
    }
    for (Uint32 i = 0; i < SDL_arraysize(lesson42_texture_expectations); i += 1) {
        if (!used_texture_references[i]) {
            SDL_SetError(
                "forge processed assets: lesson 42 expected texture '%s' is not referenced by materials",
                lesson42_texture_expectations[i].source_path);
            return false;
        }
    }
    return true;
}

static bool validate_lesson42_texture_fixture(
    const char *asset_root,
    const Lesson42TextureExpectation *expectation,
    Uint64 *total_ftex_bytes,
    Uint32 *bc7_srgb_count,
    Uint32 *bc7_unorm_count,
    Uint32 *bc5_count)
{
    char image_path[1024];
    char ftex_path[1024];
    ForgeGpuProcessedTextureCompressionInfo info;
    ForgeGpuProcessedCompressedTexture texture;
    Uint64 loaded_ftex_bytes;

    if (!build_lesson42_processed_path(asset_root, expectation->source_path, image_path, sizeof(image_path)) ||
        !ForgeGpuLoadProcessedTextureCompressionSidecar(image_path, &info)) {
        return false;
    }
    if (info.output_width != 2048 ||
        info.output_height != 2048 ||
        info.mip_count != 12 ||
        info.ftex_format != expectation->ftex_format ||
        info.normal_map != expectation->normal_map) {
        SDL_SetError("forge processed assets: lesson 42 sidecar facts did not match for '%s'", expectation->source_path);
        return false;
    }
    if (!build_lesson42_processed_path(asset_root, info.ftex_file, ftex_path, sizeof(ftex_path)) ||
        !ForgeGpuLoadProcessedFtexV1(ftex_path, &texture)) {
        return false;
    }

    loaded_ftex_bytes =
        FORGE_GPU_PROCESSED_FTEX_HEADER_SIZE +
        ((Uint64)texture.mip_count * FORGE_GPU_PROCESSED_FTEX_MIP_ENTRY_SIZE);
    for (Uint32 i = 0; i < texture.mip_count; i += 1) {
        loaded_ftex_bytes += texture.mips[i].data_size;
    }
    if (texture.width != info.output_width ||
        texture.height != info.output_height ||
        texture.mip_count != info.mip_count ||
        texture.format != info.ftex_format ||
        loaded_ftex_bytes != info.ftex_bytes) {
        SDL_SetError("forge processed assets: lesson 42 .ftex facts did not match for '%s'", expectation->source_path);
        ForgeGpuFreeProcessedCompressedTexture(&texture);
        return false;
    }

    *total_ftex_bytes += loaded_ftex_bytes;
    if (texture.format == FORGE_GPU_PROCESSED_FTEX_BC7_SRGB) {
        *bc7_srgb_count += 1;
    } else if (texture.format == FORGE_GPU_PROCESSED_FTEX_BC7_UNORM) {
        *bc7_unorm_count += 1;
    } else if (texture.format == FORGE_GPU_PROCESSED_FTEX_BC5_UNORM) {
        *bc5_count += 1;
    }
    ForgeGpuFreeProcessedCompressedTexture(&texture);
    return true;
}

static bool run_lesson42_processed_fixture_self_test(const char *asset_root)
{
    char path[1024];
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    Uint64 total_ftex_bytes = 0;
    Uint32 bc7_srgb_count = 0;
    Uint32 bc7_unorm_count = 0;
    Uint32 bc5_count = 0;

    if (!build_lesson42_processed_path(asset_root, "ABeautifulGame.fscene", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        return false;
    }
    if (scene.node_count != 49 || scene.mesh_count != 15 || scene.root_count != 33) {
        SDL_SetError("forge processed assets: lesson 42 scene facts did not match");
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    ForgeGpuFreeProcessedScene(&scene);

    if (!build_lesson42_processed_path(asset_root, "ABeautifulGame.fmesh", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        return false;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&mesh) ||
        mesh.lod_count == 0 ||
        mesh.submesh_count < 15 ||
        mesh.vertex_count == 0 ||
        mesh.total_index_count == 0) {
        SDL_SetError("forge processed assets: lesson 42 mesh facts did not match");
        ForgeGpuFreeProcessedMesh(&mesh);
        return false;
    }
    ForgeGpuFreeProcessedMesh(&mesh);

    if (!build_lesson42_processed_path(asset_root, "ABeautifulGame.fmat", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &materials)) {
        return false;
    }
    if (!validate_lesson42_processed_materials(&materials)) {
        ForgeGpuFreeProcessedMaterials(&materials);
        return false;
    }
    ForgeGpuFreeProcessedMaterials(&materials);

    for (Uint32 i = 0; i < SDL_arraysize(lesson42_texture_expectations); i += 1) {
        if (!validate_lesson42_texture_fixture(
                asset_root,
                &lesson42_texture_expectations[i],
                &total_ftex_bytes,
                &bc7_srgb_count,
                &bc7_unorm_count,
                &bc5_count)) {
            return false;
        }
    }
    if (total_ftex_bytes != 184557648u ||
        bc7_srgb_count != 13 ||
        bc7_unorm_count != 10 ||
        bc5_count != 10 ||
        ForgeGpuEstimateProcessedRgba8MipBytes(2048, 2048) != 22369620u) {
        SDL_SetError("forge processed assets: lesson 42 compressed texture aggregate facts did not match");
        return false;
    }
    return true;
}

typedef struct Lesson43ProcessedFixtureFact
{
    const char *directory;
    const char *stem;
    Uint32 expected_nodes;
    Uint32 expected_children;
    Uint32 expected_materials;
    Uint32 expected_vertices;
    Uint32 expected_submeshes;
    Uint32 expected_total_indices;
    Uint32 expected_vertex_stride;
    Uint32 expected_mesh_flags;
    Uint32 expected_skin_count;
    Uint32 expected_skin_joints;
    Uint32 expected_animation_clips;
    Uint32 expected_animation_channels;
    const char *texture_file;
    Uint32 expected_texture_width;
    Uint32 expected_texture_height;
} Lesson43ProcessedFixtureFact;

static bool build_lesson43_processed_path(
    const char *asset_root,
    const Lesson43ProcessedFixtureFact *fact,
    const char *file,
    char *path,
    size_t path_size)
{
    char relative_path[512];
    const int result = SDL_snprintf(
        relative_path,
        sizeof(relative_path),
        "processed/43-pipeline-skinned-animations/%s/%s",
        fact->directory,
        file);

    if (result <= 0 || (size_t)result >= sizeof(relative_path)) {
        SDL_SetError("forge processed assets: lesson 43 fixture path is too long");
        return false;
    }
    return join_asset_path(asset_root, relative_path, path, path_size);
}

static bool validate_lesson43_processed_fixture(
    const char *asset_root,
    const Lesson43ProcessedFixtureFact *fact)
{
    char file[128];
    char path[1024];
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    ForgeGpuProcessedSkinSet skins;
    ForgeGpuProcessedAnimation animation;
    Uint32 width = 0;
    Uint32 height = 0;

    if (SDL_snprintf(file, sizeof(file), "%s.fscene", fact->stem) <= 0 ||
        !build_lesson43_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        return false;
    }
    if (scene.node_count != fact->expected_nodes ||
        scene.mesh_count != 1 ||
        scene.root_count != 1 ||
        scene.child_count != fact->expected_children ||
        count_processed_scene_mesh_nodes(&scene) != 1) {
        SDL_SetError("forge processed assets: lesson 43 %s scene facts did not match", fact->stem);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }

    if (SDL_snprintf(file, sizeof(file), "%s.fmesh", fact->stem) <= 0 ||
        !build_lesson43_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    if (mesh.vertex_count != fact->expected_vertices ||
        mesh.vertex_stride != fact->expected_vertex_stride ||
        mesh.flags != fact->expected_mesh_flags ||
        mesh.lod_count != 3 ||
        mesh.submesh_count != fact->expected_submeshes ||
        mesh.total_index_count != fact->expected_total_indices) {
        SDL_SetError("forge processed assets: lesson 43 %s mesh facts did not match", fact->stem);
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }

    if (SDL_snprintf(file, sizeof(file), "%s.fmat", fact->stem) <= 0 ||
        !build_lesson43_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &materials)) {
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    if (materials.material_count != fact->expected_materials ||
        !ForgeGpuValidateProcessedSceneModelReferences(&scene, &mesh, &materials, fact->stem)) {
        SDL_SetError("forge processed assets: lesson 43 %s material facts did not match", fact->stem);
        ForgeGpuFreeProcessedMaterials(&materials);
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    ForgeGpuFreeProcessedMaterials(&materials);

    if (fact->expected_skin_count > 0) {
        if (SDL_snprintf(file, sizeof(file), "%s.fskin", fact->stem) <= 0 ||
            !build_lesson43_processed_path(asset_root, fact, file, path, sizeof(path)) ||
            !ForgeGpuLoadProcessedSkins(path, &skins)) {
            ForgeGpuFreeProcessedMesh(&mesh);
            ForgeGpuFreeProcessedScene(&scene);
            return false;
        }
        if (skins.skin_count != fact->expected_skin_count ||
            skins.skins[0].joint_count != fact->expected_skin_joints ||
            !ForgeGpuValidateProcessedSceneSkinReferences(&scene, &mesh, &skins, fact->stem)) {
            SDL_SetError("forge processed assets: lesson 43 %s skin facts did not match", fact->stem);
            ForgeGpuFreeProcessedSkins(&skins);
            ForgeGpuFreeProcessedMesh(&mesh);
            ForgeGpuFreeProcessedScene(&scene);
            return false;
        }
        ForgeGpuFreeProcessedSkins(&skins);
    } else if (ForgeGpuProcessedMeshIsSkinned(&mesh)) {
        SDL_SetError("forge processed assets: lesson 43 %s unexpectedly uses skinned mesh data", fact->stem);
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }

    if (SDL_snprintf(file, sizeof(file), "%s.fanim", fact->stem) <= 0 ||
        !build_lesson43_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedAnimation(path, &animation)) {
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    if (animation.clip_count != fact->expected_animation_clips ||
        animation.clips[0].channel_count != fact->expected_animation_channels ||
        !ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, fact->stem)) {
        SDL_SetError("forge processed assets: lesson 43 %s animation facts did not match", fact->stem);
        ForgeGpuFreeProcessedAnimation(&animation);
        ForgeGpuFreeProcessedMesh(&mesh);
        ForgeGpuFreeProcessedScene(&scene);
        return false;
    }
    ForgeGpuFreeProcessedAnimation(&animation);

    ForgeGpuFreeProcessedMesh(&mesh);
    ForgeGpuFreeProcessedScene(&scene);

    if (fact->texture_file) {
        if (!build_lesson43_processed_path(asset_root, fact, fact->texture_file, path, sizeof(path)) ||
            !ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
            return false;
        }
        if (width != fact->expected_texture_width || height != fact->expected_texture_height) {
            SDL_SetError("forge processed assets: lesson 43 %s texture sidecar facts did not match", fact->stem);
            return false;
        }
    }

    return true;
}

static bool run_lesson43_processed_fixture_self_test(const char *asset_root)
{
    static const Lesson43ProcessedFixtureFact facts[] = {
        {
            "CesiumMan", "CesiumMan",
            22u, 21u, 1u,
            3273u, 1u, 24528u,
            FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS,
            FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS | FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED,
            1u, 19u, 1u, 57u,
            "CesiumMan_img0.png", 1024u, 1024u
        },
        {
            "BrainStem", "BrainStem",
            22u, 21u, 59u,
            34159u, 59u, 327456u,
            FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS,
            FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS | FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED,
            1u, 18u, 1u, 57u,
            NULL, 0u, 0u
        },
        {
            "AnimatedCube", "AnimatedCube",
            1u, 0u, 1u,
            32u, 1u, 108u,
            FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS,
            FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS,
            0u, 0u, 1u, 1u,
            "AnimatedCube_BaseColor.png", 512u, 512u
        }
    };

    for (Uint32 i = 0; i < SDL_arraysize(facts); i += 1) {
        if (!validate_lesson43_processed_fixture(asset_root, &facts[i])) {
            return false;
        }
    }
    return true;
}

typedef struct Lesson44ProcessedFixtureFact
{
    const char *directory;
    const char *stem;
    bool has_materials;
    Uint32 expected_vertices;
    Uint32 expected_total_indices;
    Uint32 expected_morph_attr_flags;
} Lesson44ProcessedFixtureFact;

static bool build_lesson44_processed_path(
    const char *asset_root,
    const Lesson44ProcessedFixtureFact *fact,
    const char *file,
    char *path,
    size_t path_size)
{
    char relative_path[512];
    const int result = SDL_snprintf(
        relative_path,
        sizeof(relative_path),
        "processed/44-pipeline-morph-animations/%s/%s",
        fact->directory,
        file);

    if (result <= 0 || (size_t)result >= sizeof(relative_path)) {
        SDL_SetError("forge processed assets: lesson 44 fixture path is too long");
        return false;
    }
    return join_asset_path(asset_root, relative_path, path, path_size);
}

static bool validate_lesson44_processed_fixture(
    const char *asset_root,
    const Lesson44ProcessedFixtureFact *fact)
{
    char file[128];
    char path[1024];
    ForgeGpuProcessedScene scene;
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    ForgeGpuProcessedAnimation animation;
    bool ok = false;

    SDL_zero(scene);
    SDL_zero(mesh);
    SDL_zero(materials);
    SDL_zero(animation);

    if (SDL_snprintf(file, sizeof(file), "%s.fscene", fact->stem) <= 0 ||
        !build_lesson44_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &scene)) {
        goto done;
    }
    if (scene.node_count != 1 ||
        scene.mesh_count != 1 ||
        scene.root_count != 1 ||
        scene.child_count != 0 ||
        count_processed_scene_mesh_nodes(&scene) != 1) {
        SDL_SetError("forge processed assets: lesson 44 %s scene facts did not match", fact->stem);
        goto done;
    }

    if (SDL_snprintf(file, sizeof(file), "%s.fmesh", fact->stem) <= 0 ||
        !build_lesson44_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        goto done;
    }
    if (mesh.vertex_count != fact->expected_vertices ||
        mesh.vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS ||
        mesh.flags != (FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS | FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS) ||
        mesh.lod_count != 3 ||
        mesh.submesh_count != 1 ||
        mesh.total_index_count != fact->expected_total_indices ||
        !ForgeGpuProcessedMeshHasTangents(&mesh) ||
        !ForgeGpuProcessedMeshHasMorphs(&mesh) ||
        mesh.morph_target_count != 2 ||
        mesh.morph_attribute_flags != fact->expected_morph_attr_flags) {
        SDL_SetError("forge processed assets: lesson 44 %s mesh facts did not match", fact->stem);
        goto done;
    }

    if (fact->has_materials) {
        if (SDL_snprintf(file, sizeof(file), "%s.fmat", fact->stem) <= 0 ||
            !build_lesson44_processed_path(asset_root, fact, file, path, sizeof(path)) ||
            !ForgeGpuLoadProcessedMaterials(path, &materials)) {
            goto done;
        }
        if (materials.material_count != 1 ||
            materials.materials[0].alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE) {
            SDL_SetError("forge processed assets: lesson 44 %s material facts did not match", fact->stem);
            goto done;
        }
    } else if (mesh.submeshes[0].material_index != -1) {
        SDL_SetError("forge processed assets: lesson 44 %s expected material-less submesh", fact->stem);
        goto done;
    }
    if (!ForgeGpuValidateProcessedSceneModelReferences(&scene, &mesh, &materials, fact->stem)) {
        goto done;
    }

    if (SDL_snprintf(file, sizeof(file), "%s.fanim", fact->stem) <= 0 ||
        !build_lesson44_processed_path(asset_root, fact, file, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedAnimation(path, &animation)) {
        goto done;
    }
    if (animation.clip_count != 1 ||
        animation.clips[0].channel_count != 1 ||
        animation.clips[0].sampler_count != 1 ||
        animation.clips[0].channels[0].target_path != FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS ||
        animation.clips[0].samplers[0].value_components != mesh.morph_target_count ||
        !ForgeGpuValidateProcessedSceneAnimationReferences(&scene, &animation, fact->stem)) {
        SDL_SetError("forge processed assets: lesson 44 %s animation facts did not match", fact->stem);
        goto done;
    }

    {
        float weights[FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS];

        for (Uint32 i = 0; i < mesh.morph_target_count; i += 1) {
            weights[i] = mesh.morph_targets[i].default_weight;
        }
        if (!ForgeGpuEvaluateProcessedMorphWeights(
                &animation.clips[0],
                animation.clips[0].channels[0].target_node,
                0.5f,
                true,
                weights,
                mesh.morph_target_count)) {
            goto done;
        }
        for (Uint32 i = 0; i < mesh.morph_target_count; i += 1) {
            if (weights[i] != weights[i] || weights[i] < -0.001f || weights[i] > 1.001f) {
                SDL_SetError("forge processed assets: lesson 44 %s morph weights are out of range", fact->stem);
                goto done;
            }
        }
    }

    ok = true;

done:
    ForgeGpuFreeProcessedAnimation(&animation);
    ForgeGpuFreeProcessedMaterials(&materials);
    ForgeGpuFreeProcessedMesh(&mesh);
    ForgeGpuFreeProcessedScene(&scene);
    return ok;
}

static bool run_lesson44_processed_fixture_self_test(const char *asset_root)
{
    static const Lesson44ProcessedFixtureFact facts[] = {
        {
            "AnimatedMorphCube", "AnimatedMorphCube", true,
            24u, 108u,
            FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION |
            FORGE_GPU_PROCESSED_MORPH_ATTR_NORMAL |
            FORGE_GPU_PROCESSED_MORPH_ATTR_TANGENT
        },
        {
            "SimpleMorph", "SimpleMorph", false,
            3u, 9u,
            FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION
        }
    };

    for (Uint32 i = 0; i < SDL_arraysize(facts); i += 1) {
        if (!validate_lesson44_processed_fixture(asset_root, &facts[i])) {
            return false;
        }
    }
    return true;
}

bool ForgeGpuRunProcessedAssetSelfTest(const char *asset_root)
{
    char path[1024];
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    ForgeGpuProcessedCompressedTexture texture;
    Uint32 width = 0;
    Uint32 height = 0;

    if (!join_asset_path(asset_root, "processed/39-pipeline-processed-assets/WaterBottle.fmesh", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        return false;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&mesh) ||
        mesh.lod_count != 3 ||
        mesh.submesh_count == 0 ||
        mesh.vertex_count == 0 ||
        mesh.total_index_count == 0) {
        SDL_SetError("forge processed assets: WaterBottle self-test facts did not match");
        ForgeGpuFreeProcessedMesh(&mesh);
        return false;
    }
    ForgeGpuFreeProcessedMesh(&mesh);

    if (!join_asset_path(asset_root, "processed/39-pipeline-processed-assets/WaterBottle.fmat", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &materials)) {
        return false;
    }
    if (materials.material_count == 0 ||
        materials.materials[0].base_color_texture[0] == '\0' ||
        materials.materials[0].normal_texture[0] == '\0' ||
        materials.materials[0].metallic_roughness_texture[0] == '\0' ||
        materials.materials[0].occlusion_texture[0] == '\0' ||
        materials.materials[0].emissive_texture[0] == '\0' ||
        materials.materials[0].metallic_factor != 1.0f ||
        materials.materials[0].roughness_factor != 1.0f ||
        materials.materials[0].occlusion_strength != 1.0f ||
        materials.materials[0].alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE ||
        materials.materials[0].alpha_cutoff != 0.5f ||
        materials.materials[0].double_sided) {
        SDL_SetError("forge processed assets: WaterBottle material self-test facts did not match");
        ForgeGpuFreeProcessedMaterials(&materials);
        return false;
    }
    ForgeGpuFreeProcessedMaterials(&materials);

    if (!join_asset_path(asset_root, "processed/39-pipeline-processed-assets/WaterBottle_baseColor.png", path, sizeof(path)) ||
        !ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
        return false;
    }
    if (width != 2048 || height != 2048) {
        SDL_SetError(
            "forge processed assets: WaterBottle texture sidecar expected 2048x2048, got %ux%u",
            (unsigned int)width,
            (unsigned int)height);
        return false;
    }

    if (!join_asset_path(asset_root, "processed/39-pipeline-processed-assets/BoxTextured.fmesh", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &mesh)) {
        return false;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&mesh) ||
        mesh.lod_count == 0 ||
        mesh.vertex_count == 0 ||
        mesh.total_index_count == 0) {
        SDL_SetError("forge processed assets: BoxTextured self-test facts did not match");
        ForgeGpuFreeProcessedMesh(&mesh);
        return false;
    }
    ForgeGpuFreeProcessedMesh(&mesh);

    if (!join_asset_path(asset_root, "processed/39-pipeline-processed-assets/BoxTextured.fmat", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &materials)) {
        return false;
    }
    if (materials.material_count == 0 ||
        materials.materials[0].base_color_texture[0] == '\0' ||
        materials.materials[0].metallic_roughness_texture[0] != '\0' ||
        materials.materials[0].normal_texture[0] != '\0' ||
        materials.materials[0].metallic_factor != 0.0f ||
        materials.materials[0].roughness_factor != 1.0f ||
        materials.materials[0].emissive_factor[0] != 0.0f ||
        materials.materials[0].emissive_factor[1] != 0.0f ||
        materials.materials[0].emissive_factor[2] != 0.0f) {
        SDL_SetError("forge processed assets: BoxTextured material self-test facts did not match");
        ForgeGpuFreeProcessedMaterials(&materials);
        return false;
    }
    ForgeGpuFreeProcessedMaterials(&materials);

    if (!run_lesson41_processed_fixture_self_test(asset_root)) {
        return false;
    }

    if (!run_lesson42_processed_fixture_self_test(asset_root)) {
        return false;
    }

    if (!run_lesson43_processed_fixture_self_test(asset_root)) {
        return false;
    }

    if (!run_lesson44_processed_fixture_self_test(asset_root)) {
        return false;
    }

    if (!join_asset_path(asset_root, "processed/format-fixtures/checkerboard.ftex", path, sizeof(path)) ||
        !ForgeGpuLoadProcessedFtexV1(path, &texture)) {
        return false;
    }
    if (texture.width != 4 ||
        texture.height != 4 ||
        texture.mip_count != 1 ||
        texture.format != FORGE_GPU_PROCESSED_FTEX_BC7_SRGB ||
        texture.mips[0].width != 4 ||
        texture.mips[0].height != 4 ||
        texture.mips[0].data_size != 16 ||
        texture.mips[0].data[0] != 0x40) {
        SDL_SetError("forge processed assets: checkerboard .ftex self-test facts did not match");
        ForgeGpuFreeProcessedCompressedTexture(&texture);
        return false;
    }
    ForgeGpuFreeProcessedCompressedTexture(&texture);

    if (!run_processed_scene_reference_self_tests()) {
        return false;
    }
    if (!run_processed_skin_animation_reference_self_tests()) {
        return false;
    }

    return run_negative_processed_asset_self_tests();
}
