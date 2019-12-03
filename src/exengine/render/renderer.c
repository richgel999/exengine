#include "render.h"
#include "math/mathlib.h"

/*
  The shadow map resolution, this
  needs to be changed so that the user
  can specify a different resolution
  without modifying this header.
*/
#define SHADOW_MAP_SIZE 1024

// projections
mat4x4 projection_90deg;

// shaders
GLuint pointfbo_shader    = 0;
GLuint forward_shader     = 0;
GLuint framebuffer_shader = 0;

// framebuffers
ex_framebuffer_t *framebuffer = NULL;

void ex_render(ex_renderer_e renderer, ex_renderable_t *renderables)
{
  /* -- FIRST RUN -- */
  if (!pointfbo_shader) {
    // set up a 90 degree projection
    // mostly for omni-directional lights
    float aspect = (float)SHADOW_MAP_SIZE/(float)SHADOW_MAP_SIZE;
    mat4x4_perspective(projection_90deg, rad(90.0f), aspect, 0.1f, EX_POINT_FAR_PLANE);

    // load shaders
    pointfbo_shader  = ex_shader("pointfbo.glsl");
    framebuffer_shader = ex_shader("fboshader.glsl");
  
    // main framebuffer used by all renderers
    framebuffer = ex_framebuffer_new(0, 0);
  }
  /* --------------- */


  /* -- LIGHT DEPTH MAPS -- */
  ex_renderlist_t *point_lights = renderables->point_lights;
  ex_renderlist_t *models       = renderables->models;

  // render light depth maps
  glCullFace(GL_BACK);
  ex_shader_use(pointfbo_shader);
  for (int i=0; i<point_lights->count; i++) {
    ex_point_light_t *light = (ex_point_light_t*)point_lights->nodes[i].obj;
  
    if ((light->dynamic || light->update) && light->is_shadow && light->is_visible) {
      ex_render_point_light_begin(light, pointfbo_shader);

      // render all shadow-casting models
      for (int j=0; j<models->count; j++) {
        ex_model_t *model = (ex_model_t*)models->nodes[j].obj;

        if (!model->is_shadow)
          ex_render_model(model, pointfbo_shader);
      }
    }

  }
  /* ---------------------- */


  /* -- RENDER PASS -- */
  switch (renderer) {
    case EX_RENDERER_FORWARD: {
      ex_render_forward(renderables);
      break;
    }
    case EX_RENDERER_DEFERRED: {
      break;
    }
  }
  /* ----------------- */
}

void ex_render_forward(ex_renderable_t *renderables)
{
  // render lists
  ex_renderlist_t *point_lights = renderables->point_lights;
  ex_renderlist_t *models       = renderables->models;

  // bind main framebuffer
  glViewport(0, 0, framebuffer->width, framebuffer->height);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->fbo);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  if (!forward_shader)
    forward_shader = ex_shader("forward.glsl");

  ex_shader_use(forward_shader);

  // send camera vars to shader
  ex_camera_matrices_t *camera = renderables->camera;
  glUniformMatrix4fv(ex_uniform(forward_shader, "u_projection"), 1, GL_FALSE, camera->projection[0]);
  glUniformMatrix4fv(ex_uniform(forward_shader, "u_view"), 1, GL_FALSE, camera->view[0]);
  glUniformMatrix4fv(ex_uniform(forward_shader, "u_inverse_view"), 1, GL_FALSE, camera->inverse_view[0]);

  // first pass is ambient
  glDisable(GL_BLEND);
  glCullFace(GL_BACK);

  // do all non shadow casting lights in a single pass
  // including the one directional light
  // and lights outside of the shadow render range
  char buff[64];
  size_t pcount = 0;
  for (int i=0; i<point_lights->count; i++) {
    ex_point_light_t *light = (ex_point_light_t*)point_lights->nodes[i].obj;
  
    if (light->is_visible) {
      sprintf(buff, "u_point_lights[%ul]", (uint32_t)pcount++);
      ex_render_point_light_draw(light, forward_shader, buff);
    }
  }

  glUniform1i(ex_uniform(forward_shader, "u_point_active"), 0);
  glUniform1i(ex_uniform(forward_shader, "u_point_count"), pcount);
  glUniform1i(ex_uniform(forward_shader, "u_ambient_pass"), 1);
  for (int i=0; i<models->count; i++) {
    ex_model_t *model = (ex_model_t*)models->nodes[i].obj;

    if (!model->is_shadow)
      ex_render_model(model, forward_shader);
  }
  glUniform1i(ex_uniform(forward_shader, "u_ambient_pass"), 0);
  glUniform1i(ex_uniform(forward_shader, "u_point_count"), 0);

  // enable blending for second pass onwards
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  // render all shadow casting point lights
  glUniform1i(ex_uniform(forward_shader, "u_point_active"), 1);
  for (int i=0; i<point_lights->count; i++) {
    ex_point_light_t *light = (ex_point_light_t*)point_lights->nodes[i].obj;
  
    if (!light->is_shadow || !light->is_visible)
      continue;

    ex_render_point_light_draw(light, forward_shader, NULL);
    for (int i=0; i<models->count; i++) {
      ex_model_t *model = (ex_model_t*)models->nodes[i].obj;
      ex_render_model(model, forward_shader);
    }
  }
  glUniform1i(ex_uniform(forward_shader, "u_point_active"), 0);
  glDisable(GL_BLEND);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // render screen quad
  glViewport(0, 0, display.width, display.height);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_FRAMEBUFFER_SRGB);

  ex_shader_use(framebuffer_shader);

  glBindVertexArray(framebuffer->vao);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(ex_uniform(framebuffer_shader, "u_texture"), 0);
  glBindTexture(GL_TEXTURE_2D, framebuffer->colorbuffer);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void ex_render_model(ex_model_t *model, GLuint shader)
{
  // handle transformations
  if (!model->use_transform && model->instance_count < 2) {
    mat4x4_identity(model->transforms[0]);
    mat4x4_translate_in_place(model->transforms[0], model->position[0], model->position[1], model->position[2]);
    mat4x4_rotate_Y(model->transforms[0], model->transforms[0], rad(model->rotation[1]));
    mat4x4_rotate_X(model->transforms[0], model->transforms[0], rad(model->rotation[0]));
    mat4x4_rotate_Z(model->transforms[0], model->transforms[0], rad(model->rotation[2]));
    mat4x4_scale_aniso(model->transforms[0], model->transforms[0], model->scale, model->scale, model->scale);
  }

  // pass bone data
  GLuint has_skeleton_loc = ex_uniform(shader, "u_has_skeleton");
  glUniform1i(has_skeleton_loc, 0);

  if (model->bones != NULL && model->current_anim != NULL) {
    glUniform1i(has_skeleton_loc, 1);
    glUniformMatrix4fv(ex_uniform(shader, "u_bone_matrix"), model->bones_len, GL_TRUE, &model->skeleton[0][0][0]);
  }

  // update instancing matrix vbo
  if (!model->is_static || model->is_static == 1) {
    glBindBuffer(GL_ARRAY_BUFFER, model->instance_vbo);
    GLvoid *ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    memcpy(ptr, &model->transforms[0], model->instance_count * sizeof(mat4x4));
    glUnmapBuffer(GL_ARRAY_BUFFER);
    
    if (model->is_static)
      model->is_static = 2;
  }

  // these remain the same for each mesh
  glUniform1i(ex_uniform(shader, "u_texture"), 4);
  glUniform1i(ex_uniform(shader, "u_spec"), 5);
  glUniform1i(ex_uniform(shader, "u_norm"), 6);

  // render meshes
  for (int i=0; i<EX_MODEL_MAX_MESHES; i++) {
    if (model->meshes[i] == NULL)
      continue;

    glUniform1i(ex_uniform(shader, "u_is_lit"), model->is_lit);
    ex_render_mesh(model->meshes[i], shader, model->instance_count);
  }
}

void ex_render_mesh(ex_mesh_t *mesh, GLuint shader, size_t count)
{
  // bind vao/ebo/tex
  glBindVertexArray(mesh->VAO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->EBO);

  // diffuse
  glActiveTexture(GL_TEXTURE4);
  if (mesh->texture < 1)
    glBindTexture(GL_TEXTURE_2D, default_texture_diffuse);
  else
    glBindTexture(GL_TEXTURE_2D, mesh->texture);

  // specular
  glActiveTexture(GL_TEXTURE5);
  if (mesh->texture_spec < 1)
    glBindTexture(GL_TEXTURE_2D, default_texture_specular);
  else
    glBindTexture(GL_TEXTURE_2D, mesh->texture_spec);

  // normal
  glActiveTexture(GL_TEXTURE6);
  if (mesh->texture_norm < 1)
    glBindTexture(GL_TEXTURE_2D, default_texture_normal);
  else
    glBindTexture(GL_TEXTURE_2D, mesh->texture_norm);

  // draw mesh
  glDrawElementsInstanced(GL_TRIANGLES, mesh->icount, GL_UNSIGNED_INT, 0, count);

  // unbind buffers
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

void ex_render_point_light_begin(ex_point_light_t *light, GLuint shader)
{
  // prevent needless updates
  light->update = 0;

  // set up light transforms for cube map
  vec3 temp;
  vec3_add(temp, light->position, (vec3){1.0f, 0.0f, 0.0f});
  mat4x4_look_at(light->transform[0], light->position, temp, (vec3){0.0f, -1.0f, 0.0f});
  mat4x4_mul(light->transform[0], projection_90deg, light->transform[0]);

  vec3_add(temp, light->position, (vec3){-1.0f, 0.0f, 0.0f});
  mat4x4_look_at(light->transform[1], light->position, temp, (vec3){0.0f, -1.0f, 0.0f});
  mat4x4_mul(light->transform[1], projection_90deg, light->transform[1]);

  vec3_add(temp, light->position, (vec3){0.0f, 1.0f, 0.0f});
  mat4x4_look_at(light->transform[2], light->position, temp, (vec3){0.0f, 0.0f, 1.0f});
  mat4x4_mul(light->transform[2], projection_90deg, light->transform[2]);

  vec3_add(temp, light->position, (vec3){0.0f, -1.0f, 0.0f});
  mat4x4_look_at(light->transform[3], light->position, temp, (vec3){0.0f, 0.0f, -1.0f});
  mat4x4_mul(light->transform[3], projection_90deg, light->transform[3]);

  vec3_add(temp, light->position, (vec3){0.0f, 0.0f, 1.0f});
  mat4x4_look_at(light->transform[4], light->position, temp, (vec3){0.0f, -1.0f, 0.0f});
  mat4x4_mul(light->transform[4], projection_90deg, light->transform[4]);
  
  vec3_add(temp, light->position, (vec3){0.0f, 0.0f, -1.0f});
  mat4x4_look_at(light->transform[5], light->position, temp, (vec3){0.0f, -1.0f, 0.0f});
  mat4x4_mul(light->transform[5], projection_90deg, light->transform[5]);

  // render to depth cube map
  glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
  glBindFramebuffer(GL_FRAMEBUFFER, light->depth_map_fbo);

  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);

  // pass transform matrices to shader
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[0]"), 1, GL_FALSE, *light->transform[0]);
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[1]"), 1, GL_FALSE, *light->transform[1]);
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[2]"), 1, GL_FALSE, *light->transform[2]);
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[3]"), 1, GL_FALSE, *light->transform[3]);
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[4]"), 1, GL_FALSE, *light->transform[4]);
  glUniformMatrix4fv(ex_uniform(shader, "u_shadow_matrices[5]"), 1, GL_FALSE, *light->transform[5]);

  glUniform1f(ex_uniform(shader, "u_far_plane"), EX_POINT_FAR_PLANE);
  glUniform3fv(ex_uniform(shader, "u_light_pos"), 1, light->position);
}

void ex_render_point_light_draw(ex_point_light_t *light, GLuint shader, const char *prefix)
{
  if (light->is_shadow) {
    glUniform1i(ex_uniform(shader, "u_point_light.is_shadow"), 1);
    
    glUniform1i(ex_uniform(shader, "u_point_depth"), 7);
    
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, light->depth_map);
  } else if (prefix != NULL) {
    char buff[64];
    sprintf(buff, "%s.is_shadow", prefix);
    glUniform1i(ex_uniform(shader, buff), 0);
  }

  if (prefix != NULL) {
    char buff[64];
    sprintf(buff, "%s.far", prefix);
    glUniform1f(ex_uniform(shader,  buff), EX_POINT_FAR_PLANE);
    sprintf(buff, "%s.position", prefix);
    glUniform3fv(ex_uniform(shader, buff), 1, light->position);
    sprintf(buff, "%s.color", prefix);
    glUniform3fv(ex_uniform(shader, buff), 1, light->color);
  } else {
    glUniform1i(ex_uniform(shader,  "u_point_active"), 1);
    glUniform1f(ex_uniform(shader,  "u_point_light.far"), EX_POINT_FAR_PLANE);
    glUniform3fv(ex_uniform(shader, "u_point_light.position"), 1, light->position);
    glUniform3fv(ex_uniform(shader, "u_point_light.color"), 1, light->color);
  }
}