#define _POSIX_C_SOURCE 200809L
#include "geometry.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "stb_image_write.h"

#define MAX_VERTS 4096
#define MAX_FACES 8192
#define MESH_COLS 4

typedef struct
{
  float x, y, z;
} Vec3;

typedef struct
{
  float u, v;
} Vec2;

typedef struct
{
  int v[3];
  int vt[3];
} Face;

int
is_obj(char* path)
{
  return has_ext(path, ".obj");
}

static int
parse_face_index(char* s, int* vi, int* vti)
{
  *vi = 0;
  *vti = 0;
  char* slash = strchr(s, '/');
  *vi = atoi(s) - 1;
  if (slash) {
    if (slash[1] != '/' && slash[1] != '\0') {
      *vti = atoi(slash + 1) - 1;
    }
  }
  return *vi >= 0;
}

Cell*
cell_read_obj(char* path)
{
  char resolved[512];
  if (!resolve_path(path, resolved, sizeof(resolved))) {
    return NULL;
  }

  FILE* fp = fopen(resolved, "r");
  if (!fp) {
    return NULL;
  }

  Vec3* raw_verts = calloc(MAX_VERTS, sizeof(Vec3));
  Vec2* raw_uvs = calloc(MAX_VERTS, sizeof(Vec2));
  int raw_nv = 0, raw_nvt = 0;

  int raw_vi[MAX_FACES * 3], raw_vti[MAX_FACES * 3];
  int nf = 0;

  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    if (line[0] == 'v' && line[1] == ' ' && raw_nv < MAX_VERTS) {
      sscanf(line + 2, "%f %f %f",
             &raw_verts[raw_nv].x, &raw_verts[raw_nv].y, &raw_verts[raw_nv].z);
      raw_nv++;
    } else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ' &&
               raw_nvt < MAX_VERTS) {
      sscanf(line + 3, "%f %f", &raw_uvs[raw_nvt].u, &raw_uvs[raw_nvt].v);
      raw_nvt++;
    } else if (line[0] == 'f' && line[1] == ' ') {
      char* tok = line + 2;
      int indices[16], uv_indices[16];
      int count = 0;

      char* saveptr;
      char* t = strtok_r(tok, " \t\n", &saveptr);
      while (t && count < 16) {
        int vi, vti;
        if (parse_face_index(t, &vi, &vti)) {
          indices[count] = vi;
          uv_indices[count] = vti;
          count++;
        }
        t = strtok_r(NULL, " \t\n", &saveptr);
      }

      for (int i = 1; i + 1 < count && nf < MAX_FACES; i++) {
        raw_vi[nf * 3 + 0] = indices[0];
        raw_vi[nf * 3 + 1] = indices[i];
        raw_vi[nf * 3 + 2] = indices[i + 1];
        raw_vti[nf * 3 + 0] = uv_indices[0];
        raw_vti[nf * 3 + 1] = uv_indices[i];
        raw_vti[nf * 3 + 2] = uv_indices[i + 1];
        nf++;
      }
    }
  }
  fclose(fp);

  if (raw_nv == 0) {
    free(raw_verts);
    free(raw_uvs);
    return NULL;
  }

  Vec3* verts = calloc(MAX_VERTS, sizeof(Vec3));
  Vec2* uvs = calloc(MAX_VERTS, sizeof(Vec2));
  Face* faces = calloc(MAX_FACES, sizeof(Face));
  int nv = 0;

  int* remap = calloc(nf * 3, sizeof(int));
  int (*seen_pairs)[2] = calloc(MAX_VERTS, sizeof(int[2]));
  int nseen = 0;

  for (int fi = 0; fi < nf; fi++) {
    for (int j = 0; j < 3; j++) {
      int vi = raw_vi[fi * 3 + j];
      int vti = raw_vti[fi * 3 + j];
      int found = -1;
      for (int s = 0; s < nseen; s++) {
        if (seen_pairs[s][0] == vi && seen_pairs[s][1] == vti) {
          found = s;
          break;
        }
      }
      if (found >= 0) {
        remap[fi * 3 + j] = found;
      } else {
        if (nv < MAX_VERTS) {
          verts[nv] = raw_verts[vi];
          if (vti >= 0 && vti < raw_nvt) {
            uvs[nv] = raw_uvs[vti];
          }
          seen_pairs[nseen][0] = vi;
          seen_pairs[nseen][1] = vti;
          remap[fi * 3 + j] = nv;
          nseen++;
          nv++;
        }
      }
    }
    faces[fi].v[0] = remap[fi * 3 + 0];
    faces[fi].v[1] = remap[fi * 3 + 1];
    faces[fi].v[2] = remap[fi * 3 + 2];
  }

  free(raw_verts);
  free(raw_uvs);
  free(remap);
  free(seen_pairs);

  float minx = verts[0].x, maxx = verts[0].x;
  float miny = verts[0].y, maxy = verts[0].y;
  float minz = verts[0].z, maxz = verts[0].z;
  for (int i = 1; i < nv; i++) {
    if (verts[i].x < minx) minx = verts[i].x;
    if (verts[i].x > maxx) maxx = verts[i].x;
    if (verts[i].y < miny) miny = verts[i].y;
    if (verts[i].y > maxy) maxy = verts[i].y;
    if (verts[i].z < minz) minz = verts[i].z;
    if (verts[i].z > maxz) maxz = verts[i].z;
  }
  float rx = maxx - minx;
  float ry = maxy - miny;
  float rz = maxz - minz;
  if (rx == 0) rx = 1;
  if (ry == 0) ry = 1;
  if (rz == 0) rz = 1;

  int w = nv + MESH_COLS;
  int h = nv;
  uint8_t* pixels = malloc(w * h * 3);
  memset(pixels, 255, w * h * 3);

  for (int i = 0; i < nv; i++) {
    uint16_t nx = (uint16_t)((verts[i].x - minx) / rx * 65535);
    uint16_t ny = (uint16_t)((verts[i].y - miny) / ry * 65535);
    uint16_t nz = (uint16_t)((verts[i].z - minz) / rz * 65535);

    int p0 = (i * w + 0) * 3;
    pixels[p0] = (nx >> 8) & 0xFF;
    pixels[p0 + 1] = (ny >> 8) & 0xFF;
    pixels[p0 + 2] = (nz >> 8) & 0xFF;

    int p1 = (i * w + 1) * 3;
    pixels[p1] = nx & 0xFF;
    pixels[p1 + 1] = ny & 0xFF;
    pixels[p1 + 2] = nz & 0xFF;

    uint16_t nu = (uint16_t)(uvs[i].u * 65535);
    uint16_t nvu = (uint16_t)(uvs[i].v * 65535);

    int p2 = (i * w + 2) * 3;
    pixels[p2] = (nu >> 8) & 0xFF;
    pixels[p2 + 1] = nu & 0xFF;
    pixels[p2 + 2] = (nvu >> 8) & 0xFF;

    int p3 = (i * w + 3) * 3;
    pixels[p3] = nvu & 0xFF;
    pixels[p3 + 1] = 0;
    pixels[p3 + 2] = 0;
  }

  for (int fi = 0; fi < nf; fi++) {
    for (int e = 0; e < 3; e++) {
      int a = faces[fi].v[e];
      int b = faces[fi].v[(e + 1) % 3];
      int c = faces[fi].v[(e + 2) % 3];
      if (a < 0 || a >= nv || b < 0 || b >= nv || c < 0 || c >= nv) {
        continue;
      }
      int lo = a < b ? a : b;
      int hi = a < b ? b : a;
      int idx = (lo * w + hi + MESH_COLS) * 3;
      if (pixels[idx + 2] != 1) {
        pixels[idx] = (uint8_t)c;
        pixels[idx + 1] = 255;
        pixels[idx + 2] = 1;
      } else {
        pixels[idx + 1] = (uint8_t)c;
      }
    }
  }

  free(verts);
  free(uvs);
  free(faces);

  char out[256];
  char* dot = strrchr(path, '.');
  if (dot) {
    int base = dot - path;
    snprintf(out, sizeof(out), "%.*s_mesh.png", base, path);
  } else {
    snprintf(out, sizeof(out), "%s_mesh.png", path);
  }

  char out_path[512];
  snprintf(out_path, sizeof(out_path), "images/%s", out);
  mkdir("images", 0755);
  stbi_write_png(out_path, w, h, 3, pixels, w * 3);
  free(pixels);

  return cell_read_image(out);
}

int
cell_write_obj(Cell* cell, char* out_path, char* mtl_path)
{
  if (!cell || cell->type != VAL_IMAGE || !cell->img_data) {
    return -1;
  }

  int w = cell->img_width;
  int h = cell->img_height;
  int nv = h;

  if (w < MESH_COLS + nv) {
    return -1;
  }

  FILE* fp = fopen(out_path, "w");
  if (!fp) {
    return -1;
  }

  if (mtl_path) {
    fprintf(fp, "mtllib %s\nusemtl material0\n", mtl_path);
  }

  for (int i = 0; i < nv; i++) {
    int p0 = (i * w + 0) * 3;
    int p1 = (i * w + 1) * 3;
    uint16_t nx = ((uint16_t)cell->img_data[p0] << 8) | cell->img_data[p1];
    uint16_t ny = ((uint16_t)cell->img_data[p0 + 1] << 8) | cell->img_data[p1 + 1];
    uint16_t nz = ((uint16_t)cell->img_data[p0 + 2] << 8) | cell->img_data[p1 + 2];

    float x = nx / 65535.0f;
    float y = ny / 65535.0f;
    float z = nz / 65535.0f;
    fprintf(fp, "v %f %f %f\n", x, y, z);
  }

  for (int i = 0; i < nv; i++) {
    int p2 = (i * w + 2) * 3;
    int p3 = (i * w + 3) * 3;
    uint16_t nu = ((uint16_t)cell->img_data[p2] << 8) | cell->img_data[p2 + 1];
    uint16_t nvu = ((uint16_t)cell->img_data[p2 + 2] << 8) | cell->img_data[p3];

    float u = nu / 65535.0f;
    float v = nvu / 65535.0f;
    fprintf(fp, "vt %f %f\n", u, v);
  }

  int max_faces = nv * nv;
  int nfaces = 0;
  int (*face_list)[3] = malloc(max_faces * sizeof(int[3]));

  for (int i = 0; i < nv; i++) {
    for (int j = i + 1; j < nv; j++) {
      int idx = (i * w + j + MESH_COLS) * 3;
      if (cell->img_data[idx + 2] != 1) {
        continue;
      }
      int ks[2] = { cell->img_data[idx], cell->img_data[idx + 1] };
      for (int ki = 0; ki < 2; ki++) {
        int k = ks[ki];
        if (k >= nv || k == 255) {
          continue;
        }
        int tri[3] = { i, j, k };
        for (int a = 0; a < 2; a++) {
          for (int b = a + 1; b < 3; b++) {
            if (tri[a] > tri[b]) {
              int tmp = tri[a]; tri[a] = tri[b]; tri[b] = tmp;
            }
          }
        }
        int dup = 0;
        for (int fi = 0; fi < nfaces; fi++) {
          if (face_list[fi][0] == tri[0] &&
              face_list[fi][1] == tri[1] &&
              face_list[fi][2] == tri[2]) {
            dup = 1;
            break;
          }
        }
        if (!dup && nfaces < max_faces) {
          face_list[nfaces][0] = tri[0];
          face_list[nfaces][1] = tri[1];
          face_list[nfaces][2] = tri[2];
          nfaces++;
          fprintf(fp, "f %d/%d %d/%d %d/%d\n",
                  tri[0] + 1, tri[0] + 1,
                  tri[1] + 1, tri[1] + 1,
                  tri[2] + 1, tri[2] + 1);
        }
      }
    }
  }
  free(face_list);
  fclose(fp);
  return 0;
}
