/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *
 *---------------------------------------------------------------------
 */

#include "z_zone.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "doomtype.h"
#include "w_wad.h"
#include "m_argv.h"
#include "d_event.h"
#include "v_video.h"
#include "doomstat.h"
#include "r_bsp.h"
#include "r_main.h"
#include "r_draw.h"
#include "r_sky.h"
#include "r_plane.h"
#include "r_data.h"
#include "p_maputl.h"
#include "p_tick.h"
#include "m_bbox.h"
#include "lprintf.h"
#include "gl_intern.h"
#include "gl_struct.h"
#include "p_spec.h"
#include "e6y.h"

/* TEXTURES */
static GLTexture **gld_GLTextures=NULL;
/* PATCHES FLATS SPRITES */
static GLTexture **gld_GLPatchTextures=NULL;
static GLTexture **gld_GLStaticPatchTextures=NULL;

boolean use_mipmapping=false;

int gld_max_texturesize=0;
char *gl_tex_format_string;
//int gl_tex_format=GL_RGBA8;
int gl_tex_format=GL_RGB5_A1;
//int gl_tex_format=GL_RGBA4;
//int gl_tex_format=GL_RGBA2;

int gl_boom_colormaps = -1;
int gl_boom_colormaps_default;

GLTexture *last_gltexture=NULL;
int last_cm=-1;

int test_voodoo;

int transparent_pal_index;
unsigned char gld_palmap[256];

void gld_ResetLastTexture(void)
{
  last_gltexture = NULL;
}

void gld_InitPalettedTextures(void)
{
  const unsigned char *playpal;
  int pal[256];
  int i,j;

  playpal=W_CacheLumpName("PLAYPAL");
  for (i=0; i<256; i++) {
    pal[i] = (playpal[i*3+0] << 16) | (playpal[i*3+1] << 8) | playpal[i*3+2];
    gld_palmap[i] = i;
  }
  transparent_pal_index = -1;
  for (i=0; i<256; i++) {
    for (j=i+1; j<256; j++) {
      if (pal[i] == pal[j]) {
        transparent_pal_index = j;
        gld_palmap[j] = i;
        break;
      }
    }
    if (transparent_pal_index >= 0)
      break;
  }
  W_UnlockLumpName("PLAYPAL");
}

int gld_GetTexDimension(int value)
{
  int i;

  i=1;
  while (i<value)
    i*=2;
  if (i>gld_max_texturesize)
    i=gld_max_texturesize;
  return i;
}

// e6y
// Creates TIntDynArray
void* NewIntDynArray(int dimCount, int *dims)
{
  int i, dim;
  int tableOffset;
  int bufferSize = 0;
  int tableSize = 1;
  void* buffer;

  for(dim = 0; dim < dimCount - 1; dim++)
  {
    tableSize *= dims[dim];
    bufferSize += sizeof(int*) * tableSize;
  }

  bufferSize += sizeof(int) * tableSize * dims[dimCount - 1];

  buffer = malloc(bufferSize);
  memset(buffer, 0, bufferSize);

  if(!buffer)
  {
    return 0;
  }

  tableOffset = 0;
  tableSize = 1;

  for(dim = 0; dim < dimCount - 1; dim++)
  {
    tableSize *= dims[dim];
    tableOffset += tableSize;

    for(i = 0; i < tableSize; i++)
    {
      if(dim < dimCount - 2)
      {
        *((int***)buffer + tableOffset - tableSize + i) =
          (((int**)buffer + tableOffset + i*dims[dim + 1]));
      }
      else
      {
        *((int**)buffer + tableOffset - tableSize + i) =
          ((int*)((int**)buffer + tableOffset) + i*dims[dim + 1]);
      }
    }
  }

  return buffer;
}

// e6y
// The common function for adding textures and patches
// Used by gld_AddNewGLTexture and gld_AddNewGLPatchTexture
static GLTexture *gld_AddNewGLTexItem(int num, int count, GLTexture ***items)
{
  if (num<0)
    return NULL;
  if (num>=count)
    return NULL;
  if (!(*items))
  {
    (*items)=Z_Calloc(count, sizeof(GLTexture *),PU_STATIC,0);
  }
  if (!(*items)[num])
  {
    (*items)[num]=Z_Calloc(1, sizeof(GLTexture),PU_STATIC,0);
    (*items)[num]->textype=GLDT_UNREGISTERED;

    if (gl_boom_colormaps)
    {
      GLTexture *texture = (*items)[num];
      int dims[3] = {(CR_LIMIT+MAXPLAYERS), (NUMCOLORMAPS+1), numcolormaps};
      texture->glTexExID = NewIntDynArray(3, dims);
    }
  }
  return (*items)[num];
}

static GLTexture *gld_AddNewGLTexture(int texture_num)
{
  return gld_AddNewGLTexItem(texture_num, numtextures, &gld_GLTextures);
}

static GLTexture *gld_AddNewGLPatchTexture(int lump)
{
  if (lumpinfo[lump].flags & LUMP_STATIC)
    return gld_AddNewGLTexItem(lump, numlumps, &gld_GLStaticPatchTextures);
  else
    return gld_AddNewGLTexItem(lump, numlumps, &gld_GLPatchTextures);
}

void gld_SetTexturePalette(GLenum target)
{
  const unsigned char *playpal;
  unsigned char pal[1024];
  int i;

  playpal=W_CacheLumpName("PLAYPAL");
  for (i=0; i<256; i++) {
    pal[i*4+0] = playpal[i*3+0];
    pal[i*4+1] = playpal[i*3+1];
    pal[i*4+2] = playpal[i*3+2];
    pal[i*4+3] = 255;
  }
  pal[transparent_pal_index*4+0]=0;
  pal[transparent_pal_index*4+1]=0;
  pal[transparent_pal_index*4+2]=0;
  pal[transparent_pal_index*4+3]=0;
  gld_ColorTableEXT(target, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, pal);
  W_UnlockLumpName("PLAYPAL");
}

static void gld_AddPatchToTexture_UnTranslated(GLTexture *gltexture, unsigned char *buffer, const rpatch_t *patch, int originx, int originy, int paletted)
{
  int x,y,j;
  int xs,xe;
  int js,je;
  const rcolumn_t *column;
  const byte *source;
  int i, pos;
  const unsigned char *playpal;

  if (!gltexture)
    return;
  if (!patch)
    return;
  playpal=W_CacheLumpName("PLAYPAL");
  xs=0;
  xe=patch->width;
  if ((xs+originx)>=gltexture->realtexwidth)
    return;
  if ((xe+originx)<=0)
    return;
  if ((xs+originx)<0)
    xs=-originx;
  if ((xe+originx)>gltexture->realtexwidth)
    xe+=(gltexture->realtexwidth-(xe+originx));
  
  //e6y
  if (patch->flags&PATCH_HASHOLES)
    gltexture->flags |= GLTEXTURE_HASHOLES;

  for (x=xs;x<xe;x++)
  {
#ifdef RANGECHECK
    if (x>=patch->width)
    {
      lprintf(LO_ERROR,"gld_AddPatchToTexture_UnTranslated x>=patch->width (%i >= %i)\n",x,patch->width);
      return;
    }
#endif
    column = &patch->columns[x];
    for (i=0; i<column->numPosts; i++) {
      const rpost_t *post = &column->posts[i];
      y=(post->topdelta+originy);
      js=0;
      je=post->length;
      if ((js+y)>=gltexture->realtexheight)
        continue;
      if ((je+y)<=0)
        continue;
      if ((js+y)<0)
        js=-y;
      if ((je+y)>gltexture->realtexheight)
        je+=(gltexture->realtexheight-(je+y));
      source = column->pixels + post->topdelta;
      if (paletted) {
        pos=(((js+y)*gltexture->buffer_width)+x+originx);
        for (j=js;j<je;j++,pos+=(gltexture->buffer_width))
        {
#ifdef RANGECHECK
          if (pos>=gltexture->buffer_size)
          {
            lprintf(LO_ERROR,"gld_AddPatchToTexture_UnTranslated pos>=size (%i >= %i)\n",pos+3,gltexture->buffer_size);
            return;
          }
#endif
          buffer[pos]=gld_palmap[source[j]];
        }
      } else {
        pos=4*(((js+y)*gltexture->buffer_width)+x+originx);
        for (j=js;j<je;j++,pos+=(4*gltexture->buffer_width))
        {
#ifdef RANGECHECK
          if ((pos+3)>=gltexture->buffer_size)
          {
            lprintf(LO_ERROR,"gld_AddPatchToTexture_UnTranslated pos+3>=size (%i >= %i)\n",pos+3,gltexture->buffer_size);
            return;
          }
#endif
          //e6y: Boom's color maps
          if (gl_boom_colormaps && use_boom_cm && !(comp[comp_skymap] && (gltexture->flags&GLTEXTURE_SKY)))
          {
            const lighttable_t *colormap = (fixedcolormap ? fixedcolormap : fullcolormap);
            buffer[pos+0]=playpal[colormap[source[j]]*3+0];
            buffer[pos+1]=playpal[colormap[source[j]]*3+1];
            buffer[pos+2]=playpal[colormap[source[j]]*3+2];
          }
          else
          {
            buffer[pos+0]=playpal[source[j]*3+0];
            buffer[pos+1]=playpal[source[j]*3+1];
            buffer[pos+2]=playpal[source[j]*3+2];
          }
          buffer[pos+3]=255;
        }
      }
    }
  }
  W_UnlockLumpName("PLAYPAL");
}

void gld_AddPatchToTexture(GLTexture *gltexture, unsigned char *buffer, const rpatch_t *patch, int originx, int originy, int cm, int paletted)
{
  int x,y,j;
  int xs,xe;
  int js,je;
  const rcolumn_t *column;
  const byte *source;
  int i, pos;
  const unsigned char *playpal;
  const unsigned char *outr;

  if (!gltexture)
    return;
  if (!patch)
    return;
  if ((cm==CR_DEFAULT) || (cm==CR_LIMIT))
  {
    gld_AddPatchToTexture_UnTranslated(gltexture,buffer,patch,originx,originy, paletted);
    return;
  }
  if (cm<CR_LIMIT)
    outr=colrngs[cm];
  else
    outr=translationtables + 256*((cm-CR_LIMIT)-1);
  playpal=W_CacheLumpName("PLAYPAL");
  xs=0;
  xe=patch->width;
  if ((xs+originx)>=gltexture->realtexwidth)
    return;
  if ((xe+originx)<=0)
    return;
  if ((xs+originx)<0)
    xs=-originx;
  if ((xe+originx)>gltexture->realtexwidth)
    xe+=(gltexture->realtexwidth-(xe+originx));

  //e6y
  if (patch->flags&PATCH_HASHOLES)
    gltexture->flags |= GLTEXTURE_HASHOLES;

  for (x=xs;x<xe;x++)
  {
#ifdef RANGECHECK
    if (x>=patch->width)
    {
      lprintf(LO_ERROR,"gld_AddPatchToTexture x>=patch->width (%i >= %i)\n",x,patch->width);
      return;
    }
#endif
    column = &patch->columns[x];
    for (i=0; i<column->numPosts; i++) {
      const rpost_t *post = &column->posts[i];
      y=(post->topdelta+originy);
      js=0;
      je=post->length;
      if ((js+y)>=gltexture->realtexheight)
        continue;
      if ((je+y)<=0)
        continue;
      if ((js+y)<0)
        js=-y;
      if ((je+y)>gltexture->realtexheight)
        je+=(gltexture->realtexheight-(je+y));
      source = column->pixels + post->topdelta;
      if (paletted) {
        pos=(((js+y)*gltexture->buffer_width)+x+originx);
        for (j=js;j<je;j++,pos+=(gltexture->buffer_width))
        {
#ifdef RANGECHECK
          if (pos>=gltexture->buffer_size)
          {
            lprintf(LO_ERROR,"gld_AddPatchToTexture_UnTranslated pos>=size (%i >= %i)\n",pos+3,gltexture->buffer_size);
            return;
          }
#endif
          buffer[pos]=gld_palmap[outr[source[j]]];
        }
      } else {
        pos=4*(((js+y)*gltexture->buffer_width)+x+originx);
        for (j=js;j<je;j++,pos+=(4*gltexture->buffer_width))
        {
#ifdef RANGECHECK
          if ((pos+3)>=gltexture->buffer_size)
          {
            lprintf(LO_ERROR,"gld_AddPatchToTexture pos+3>=size (%i >= %i)\n",pos+3,gltexture->buffer_size);
            return;
          }
#endif
          //e6y: Boom's color maps
          if (gl_boom_colormaps && use_boom_cm)
          {
            const lighttable_t *colormap = (fixedcolormap ? fixedcolormap : fullcolormap);
            buffer[pos+0]=playpal[colormap[outr[source[j]]]*3+0];
            buffer[pos+1]=playpal[colormap[outr[source[j]]]*3+1];
            buffer[pos+2]=playpal[colormap[outr[source[j]]]*3+2];
          }
          else
          {
            buffer[pos+0]=playpal[outr[source[j]]*3+0];
            buffer[pos+1]=playpal[outr[source[j]]*3+1];
            buffer[pos+2]=playpal[outr[source[j]]*3+2];
          }
          buffer[pos+3]=255;
        }
      }
    }
  }
  W_UnlockLumpName("PLAYPAL");
}

static void gld_AddFlatToTexture(GLTexture *gltexture, unsigned char *buffer, const unsigned char *flat, int paletted)
{
  int x,y,pos;
  const unsigned char *playpal;

  if (!gltexture)
    return;
  if (!flat)
    return;
  if (paletted) {
    for (y=0;y<gltexture->realtexheight;y++)
    {
      pos=(y*gltexture->buffer_width);
      for (x=0;x<gltexture->realtexwidth;x++,pos++)
      {
#ifdef RANGECHECK
        if (pos>=gltexture->buffer_size)
        {
          lprintf(LO_ERROR,"gld_AddFlatToTexture pos>=size (%i >= %i)\n",pos,gltexture->buffer_size);
          return;
        }
#endif
        buffer[pos]=gld_palmap[flat[y*64+x]];
      }
    }
  } else {
    playpal=W_CacheLumpName("PLAYPAL");
    for (y=0;y<gltexture->realtexheight;y++)
    {
      pos=4*(y*gltexture->buffer_width);
      for (x=0;x<gltexture->realtexwidth;x++,pos+=4)
      {
#ifdef RANGECHECK
        if ((pos+3)>=gltexture->buffer_size)
        {
          lprintf(LO_ERROR,"gld_AddFlatToTexture pos+3>=size (%i >= %i)\n",pos+3,gltexture->buffer_size);
          return;
        }
#endif
        //e6y: Boom's color maps
        if (gl_boom_colormaps && use_boom_cm)
        {
          const lighttable_t *colormap = (fixedcolormap ? fixedcolormap : fullcolormap);
          buffer[pos+0]=playpal[colormap[flat[y*64+x]]*3+0];
          buffer[pos+1]=playpal[colormap[flat[y*64+x]]*3+1];
          buffer[pos+2]=playpal[colormap[flat[y*64+x]]*3+2];
        }
        else
        {
          buffer[pos+0]=playpal[flat[y*64+x]*3+0];
          buffer[pos+1]=playpal[flat[y*64+x]*3+1];
          buffer[pos+2]=playpal[flat[y*64+x]*3+2];
        }
        buffer[pos+3]=255;
      }
    }
    W_UnlockLumpName("PLAYPAL");
  }
}

//e6y: "force" flag for loading texture with zero index
GLTexture *gld_RegisterTexture(int texture_num, boolean mipmap, boolean force)
{
  GLTexture *gltexture;

  //e6y: textures with zero index should be loaded sometimes
  if (texture_num==NO_TEXTURE && !force)
    return NULL;
  gltexture=gld_AddNewGLTexture(texture_num);
  if (!gltexture)
    return NULL;
  if (gltexture->textype==GLDT_UNREGISTERED)
  {
    texture_t *texture=NULL;

    if ((texture_num>=0) || (texture_num<numtextures))
      texture=textures[texture_num];
    if (!texture)
      return NULL;
    gltexture->textype=GLDT_BROKEN;
    gltexture->index=texture_num;
    gltexture->mipmap=mipmap;
    gltexture->realtexwidth=texture->width;
    gltexture->realtexheight=texture->height;
    gltexture->leftoffset=0;
    gltexture->topoffset=0;
    gltexture->tex_width=gld_GetTexDimension(gltexture->realtexwidth);
    gltexture->tex_height=gld_GetTexDimension(gltexture->realtexheight);
    gltexture->width=MIN(gltexture->realtexwidth, gltexture->tex_width);
    gltexture->height=MIN(gltexture->realtexheight, gltexture->tex_height);
    gltexture->buffer_width=gltexture->tex_width;
    gltexture->buffer_height=gltexture->tex_height;
#ifdef USE_GLU_IMAGESCALE
    gltexture->width=gltexture->tex_width;
    gltexture->height=gltexture->tex_height;
    gltexture->buffer_width=gltexture->realtexwidth;
    gltexture->buffer_height=gltexture->realtexheight;
#endif
    if (gltexture->mipmap & use_mipmapping)
    {
      gltexture->width=gltexture->tex_width;
      gltexture->height=gltexture->tex_height;
      gltexture->buffer_width=gltexture->realtexwidth;
      gltexture->buffer_height=gltexture->realtexheight;
    }
    
    //e6y: right/bottom UV coordinates for texture drawing
    gltexture->scalexfac=(float)gltexture->width/(float)gltexture->tex_width;
    gltexture->scaleyfac=(float)gltexture->height/(float)gltexture->tex_height;

    gltexture->buffer_size=gltexture->buffer_width*gltexture->buffer_height*4;
    if (gltexture->realtexwidth>gltexture->buffer_width)
      return gltexture;
    if (gltexture->realtexheight>gltexture->buffer_height)
      return gltexture;
    gltexture->textype=GLDT_TEXTURE;
    gltexture->flags = 0;//e6y
  }
  return gltexture;
}

void gld_BindTexture(GLTexture *gltexture)
{
  const rpatch_t *patch;
  int i;
  unsigned char *buffer;
  int *glTexID;

  if (gltexture==last_gltexture && boom_cm==last_boom_cm && frame_fixedcolormap==last_fixedcolormap)
    return;
  last_gltexture=gltexture;
  last_fixedcolormap=frame_fixedcolormap;
  if (!gltexture) {
    glBindTexture(GL_TEXTURE_2D, 0);
    last_gltexture = NULL;
    last_cm = -1;
    return;
  }
  if (gltexture->textype!=GLDT_TEXTURE)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    last_gltexture = NULL;
    last_cm = -1;
    return;
  }

  //e6y
  if (gl_boom_colormaps)
    glTexID = &gltexture->glTexExID[CR_DEFAULT][frame_fixedcolormap][boom_cm];
  else
    glTexID = &gltexture->glTexID[CR_DEFAULT];

  if (*glTexID!=0)
  {
    glBindTexture(GL_TEXTURE_2D, *glTexID);

     // e6y: old unnecessary code under check now
    if (!test_voodoo)
      return;

    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_RESIDENT,&i);
#ifdef _DEBUG
    if (i!=GL_TRUE)
      lprintf(LO_INFO, "glGetTexParam: %i\n", i);
#endif
    if (i==GL_TRUE)
      return;
  }

  if (gld_LoadHiresTex(gltexture, glTexID, CR_DEFAULT))
    return;

  buffer=(unsigned char*)Z_Malloc(gltexture->buffer_size,PU_STATIC,0);
  if (!(gltexture->mipmap & use_mipmapping) & gl_paletted_texture)
    memset(buffer,transparent_pal_index,gltexture->buffer_size);
  else
    memset(buffer,0,gltexture->buffer_size);
  patch=R_CacheTextureCompositePatchNum(gltexture->index);
  gld_AddPatchToTexture(gltexture, buffer, patch,
                        0, 0,
                        CR_DEFAULT, !(gltexture->mipmap & use_mipmapping) & gl_paletted_texture);
  R_UnlockTextureCompositePatchNum(gltexture->index);
  if (*glTexID==0)
    glGenTextures(1,glTexID);
  glBindTexture(GL_TEXTURE_2D, *glTexID);
#ifdef USE_GLU_MIPMAP
  if (gltexture->mipmap & use_mipmapping)
  {
    gluBuild2DMipmaps(GL_TEXTURE_2D, gl_tex_format,
                      gltexture->buffer_width, gltexture->buffer_height,
                      GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_mipmap_filter);
    if (gl_use_texture_filter_anisotropic)
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)gl_texture_filter_anisotropic);
  }
  else
#endif /* USE_GLU_MIPMAP */
  {
#ifdef USE_GLU_IMAGESCALE
    if ((gltexture->buffer_width!=gltexture->tex_width) ||
        (gltexture->buffer_height!=gltexture->tex_height)
       )
    {
      unsigned char *scaledbuffer;

      scaledbuffer=(unsigned char*)Z_Malloc(gltexture->tex_width*gltexture->tex_height*4,PU_STATIC,0);
      if (scaledbuffer)
      {
        gluScaleImage(GL_RGBA,
                      gltexture->buffer_width, gltexture->buffer_height,
                      GL_UNSIGNED_BYTE,buffer,
                      gltexture->tex_width, gltexture->tex_height,
                      GL_UNSIGNED_BYTE,scaledbuffer);
        Z_Free(buffer);
        buffer=scaledbuffer;
        glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                      gltexture->tex_width, gltexture->tex_height,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
      }
    }
    else
#endif /* USE_GLU_IMAGESCALE */
    {
      if (gl_paletted_texture) {
        gld_SetTexturePalette(GL_TEXTURE_2D);
        glTexImage2D( GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, buffer);
      } else {
        glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
      }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_tex_filter);
  }
  Z_Free(buffer);
}

GLTexture *gld_RegisterPatch(int lump, int cm)
{
  const rpatch_t *patch;
  GLTexture *gltexture;

  gltexture=gld_AddNewGLPatchTexture(lump);
  if (!gltexture)
    return NULL;
  if (gltexture->textype==GLDT_UNREGISTERED)
  {
    patch=R_CachePatchNum(lump);
    if (!patch)
      return NULL;
    gltexture->textype=GLDT_BROKEN;
    gltexture->index=lump;
    gltexture->mipmap=false;
    
    //e6y
    gltexture->wrap_mode = (patch->flags&PATCH_REPEAT?
      (GL_REPEAT) :
      (glversion >= OPENGL_VERSION_1_2 ? GL_CLAMP_TO_EDGE : GL_CLAMP));

    gltexture->realtexwidth=patch->width;
    gltexture->realtexheight=patch->height;
    gltexture->leftoffset=patch->leftoffset;
    gltexture->topoffset=patch->topoffset;
    gltexture->tex_width=gld_GetTexDimension(gltexture->realtexwidth);
    gltexture->tex_height=gld_GetTexDimension(gltexture->realtexheight);
    gltexture->width=MIN(gltexture->realtexwidth, gltexture->tex_width);
    gltexture->height=MIN(gltexture->realtexheight, gltexture->tex_height);
    gltexture->buffer_width=gltexture->tex_width;
    gltexture->buffer_height=gltexture->tex_height;
#ifdef USE_GLU_IMAGESCALE
    gltexture->width=MIN(gltexture->realtexwidth, gltexture->tex_width);
    gltexture->height=MIN(gltexture->realtexheight, gltexture->tex_height);
    gltexture->buffer_width=MAX(gltexture->realtexwidth, gltexture->tex_width);
    gltexture->buffer_height=MAX(gltexture->realtexheight, gltexture->tex_height);
#endif

    //e6y: right/bottom UV coordinates for patch drawing
    gltexture->scalexfac=(float)gltexture->width/(float)gltexture->tex_width;
    gltexture->scaleyfac=(float)gltexture->height/(float)gltexture->tex_height;

    gltexture->buffer_size=gltexture->buffer_width*gltexture->buffer_height*4;
    R_UnlockPatchNum(lump);
    if (gltexture->realtexwidth>gltexture->buffer_width)
      return gltexture;
    if (gltexture->realtexheight>gltexture->buffer_height)
      return gltexture;
    gltexture->textype=GLDT_PATCH;
  }
  return gltexture;
}

void gld_BindPatch(GLTexture *gltexture, int cm)
{
  const rpatch_t *patch;
  int i;
  unsigned char *buffer;
  int *glTexID;

  cm = ((gltexture->flags & GLTEXTURE_HIRES) ? CR_DEFAULT : cm);

  if ((gltexture==last_gltexture) && (cm==last_cm) && (boom_cm==last_boom_cm) && (frame_fixedcolormap==last_fixedcolormap))
    return;
  last_gltexture=gltexture;
  last_cm=cm;
  last_fixedcolormap=frame_fixedcolormap;
  if (!gltexture)
    return;
  if (gltexture->textype!=GLDT_PATCH)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    last_gltexture = NULL;
    last_cm = -1;
    return;
  }

  //e6y
  if (gl_boom_colormaps)
    glTexID = &gltexture->glTexExID[cm][frame_fixedcolormap][boom_cm];
  else
    glTexID = &gltexture->glTexID[cm];

  if (*glTexID!=0)
  {
    glBindTexture(GL_TEXTURE_2D, *glTexID);

    // e6y: old unnecessary code under check now
    if (!test_voodoo)
      return;

    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_RESIDENT,&i);
#ifdef _DEBUG
    if (i!=GL_TRUE)
      lprintf(LO_INFO, "glGetTexParam: %i\n", i);
#endif
    if (i==GL_TRUE)
      return;
  }

  if (gld_LoadHiresTex(gltexture, glTexID, cm))
    return;

  patch=R_CachePatchNum(gltexture->index);
  buffer=(unsigned char*)Z_Malloc(gltexture->buffer_size,PU_STATIC,0);
  if (gl_paletted_texture)
    memset(buffer,transparent_pal_index,gltexture->buffer_size);
  else
    memset(buffer,0,gltexture->buffer_size);
  gld_AddPatchToTexture(gltexture, buffer, patch, 0, 0, cm, gl_paletted_texture);

  // e6y
  // Post-process the texture data after the buffer has been created.
  // Smooth the edges of transparent fields in the texture.
  //
  // It is a workaround to set the color of all transparent pixels
  // that border on a non-transparent pixel to the color
  // of one bordering non-transparent pixel.
  // It is necessary for textures that are not power of two
  // to avoid the lines (boxes) around the elements that change
  // on the intermission screens in Doom1 (E2, E3)
  if (gltexture->tex_width != gltexture->realtexwidth || 
      gltexture->tex_height != gltexture->realtexheight)
  {
    SmoothEdges(buffer, gltexture->tex_width, gltexture->tex_height);
  }

  if (*glTexID==0)
    glGenTextures(1,glTexID);
  glBindTexture(GL_TEXTURE_2D, *glTexID);
#ifdef USE_GLU_IMAGESCALE
  if ((gltexture->buffer_width>gltexture->tex_width) ||
      (gltexture->buffer_height>gltexture->tex_height)
     )
  {
    unsigned char *scaledbuffer;

    scaledbuffer=(unsigned char*)Z_Malloc(gltexture->tex_width*gltexture->tex_height*4,PU_STATIC,0);
    if (scaledbuffer)
    {
      gluScaleImage(GL_RGBA,
                    gltexture->buffer_width, gltexture->buffer_height,
                    GL_UNSIGNED_BYTE,buffer,
                    gltexture->tex_width, gltexture->tex_height,
                    GL_UNSIGNED_BYTE,scaledbuffer);
      Z_Free(buffer);
      buffer=scaledbuffer;
      glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                    gltexture->tex_width, gltexture->tex_height,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    }
  }
  else
#endif /* USE_GLU_IMAGESCALE */
  {
      if (gl_paletted_texture) {
        gld_SetTexturePalette(GL_TEXTURE_2D);
        glTexImage2D( GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, buffer);
      } else {
        glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
      }
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gltexture->wrap_mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gltexture->wrap_mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_tex_filter);
  Z_Free(buffer);
  R_UnlockPatchNum(gltexture->index);
}

GLTexture *gld_RegisterFlat(int lump, boolean mipmap)
{
  GLTexture *gltexture;

  gltexture=gld_AddNewGLPatchTexture(firstflat+lump);
  if (!gltexture)
    return NULL;
  if (gltexture->textype==GLDT_UNREGISTERED)
  {
    gltexture->textype=GLDT_BROKEN;
    gltexture->index=firstflat+lump;
    gltexture->mipmap=mipmap;
    gltexture->realtexwidth=64;
    gltexture->realtexheight=64;
    gltexture->leftoffset=0;
    gltexture->topoffset=0;
    gltexture->tex_width=gld_GetTexDimension(gltexture->realtexwidth);
    gltexture->tex_height=gld_GetTexDimension(gltexture->realtexheight);
    gltexture->width=MIN(gltexture->realtexwidth, gltexture->tex_width);
    gltexture->height=MIN(gltexture->realtexheight, gltexture->tex_height);
    gltexture->buffer_width=gltexture->tex_width;
    gltexture->buffer_height=gltexture->tex_height;
#ifdef USE_GLU_IMAGESCALE
    gltexture->width=gltexture->tex_width;
    gltexture->height=gltexture->tex_height;
    gltexture->buffer_width=gltexture->realtexwidth;
    gltexture->buffer_height=gltexture->realtexheight;
#endif
    if (gltexture->mipmap & use_mipmapping)
    {
      gltexture->width=gltexture->tex_width;
      gltexture->height=gltexture->tex_height;
      gltexture->buffer_width=gltexture->realtexwidth;
      gltexture->buffer_height=gltexture->realtexheight;
    }

    //e6y: right/bottom UV coordinates for flat drawing
    gltexture->scalexfac=(float)gltexture->width/(float)gltexture->tex_width;
    gltexture->scaleyfac=(float)gltexture->height/(float)gltexture->tex_height;

    gltexture->buffer_size=gltexture->buffer_width*gltexture->buffer_height*4;
    if (gltexture->realtexwidth>gltexture->buffer_width)
      return gltexture;
    if (gltexture->realtexheight>gltexture->buffer_height)
      return gltexture;
    gltexture->textype=GLDT_FLAT;
  }
  return gltexture;
}

void gld_BindFlat(GLTexture *gltexture)
{
  const unsigned char *flat;
  int i;
  unsigned char *buffer;
  int *glTexID;

  if (gltexture==last_gltexture && boom_cm==last_boom_cm && frame_fixedcolormap==last_fixedcolormap)
    return;
  last_gltexture=gltexture;
  last_fixedcolormap=frame_fixedcolormap;
  if (!gltexture)
    return;
  if (gltexture->textype!=GLDT_FLAT)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    last_gltexture = NULL;
    last_cm = -1;
    return;
  }

  //e6y
  if (gl_boom_colormaps)
    glTexID = &gltexture->glTexExID[CR_DEFAULT][frame_fixedcolormap][boom_cm];
  else
    glTexID = &gltexture->glTexID[CR_DEFAULT];

  if (*glTexID!=0)
  {
    glBindTexture(GL_TEXTURE_2D, *glTexID);
    
    // e6y: old unnecessary code under check now
    if (!test_voodoo)
      return;

    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_RESIDENT,&i);
#ifdef _DEBUG
    if (i!=GL_TRUE)
      lprintf(LO_INFO, "glGetTexParam: %i\n", i);
#endif
    if (i==GL_TRUE)
      return;
  }

  if (gld_LoadHiresTex(gltexture, glTexID, CR_DEFAULT))
    return;

  flat=W_CacheLumpNum(gltexture->index);
  buffer=(unsigned char*)Z_Malloc(gltexture->buffer_size,PU_STATIC,0);
  if (!(gltexture->mipmap & use_mipmapping) & gl_paletted_texture)
    memset(buffer,transparent_pal_index,gltexture->buffer_size);
  else
    memset(buffer,0,gltexture->buffer_size);
  gld_AddFlatToTexture(gltexture, buffer, flat, !(gltexture->mipmap & use_mipmapping) & gl_paletted_texture);
  if (*glTexID==0)
    glGenTextures(1,glTexID);
  glBindTexture(GL_TEXTURE_2D, *glTexID);
#ifdef USE_GLU_MIPMAP
  if (gltexture->mipmap & use_mipmapping)
  {
    gluBuild2DMipmaps(GL_TEXTURE_2D, gl_tex_format,
                      gltexture->buffer_width, gltexture->buffer_height,
                      GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_mipmap_filter);
    if (gl_use_texture_filter_anisotropic)
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)gl_texture_filter_anisotropic);
  }
  else
#endif /* USE_GLU_MIPMAP */
  {
#ifdef USE_GLU_IMAGESCALE
    if ((gltexture->buffer_width!=gltexture->tex_width) ||
        (gltexture->buffer_height!=gltexture->tex_height)
       )
    {
      unsigned char *scaledbuffer;

      scaledbuffer=(unsigned char*)Z_Malloc(gltexture->tex_width*gltexture->tex_height*4,PU_STATIC,0);
      if (scaledbuffer)
      {
        gluScaleImage(GL_RGBA,
                      gltexture->buffer_width, gltexture->buffer_height,
                      GL_UNSIGNED_BYTE,buffer,
                      gltexture->tex_width, gltexture->tex_height,
                      GL_UNSIGNED_BYTE,scaledbuffer);
        Z_Free(buffer);
        buffer=scaledbuffer;
        glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                      gltexture->tex_width, gltexture->tex_height,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
      }
    }
    else
#endif /* USE_GLU_IMAGESCALE */
    {
      if (gl_paletted_texture) {
        gld_SetTexturePalette(GL_TEXTURE_2D);
        glTexImage2D( GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, buffer);
      } else {
        glTexImage2D( GL_TEXTURE_2D, 0, gl_tex_format,
                      gltexture->buffer_width, gltexture->buffer_height,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
      }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_tex_filter);
  }
  Z_Free(buffer);
  W_UnlockLumpNum(gltexture->index);
}

// e6y
// The common function for cleaning textures and patches
// gld_CleanTextures and gld_CleanPatchTextures are replaced with that
static void gld_CleanTexItems(int count, GLTexture ***items)
{
  int i, j;

  if (!(*items))
    return;
  for (i=0; i<count; i++)
  {
    if ((*items)[i])
    {
      if (gl_boom_colormaps)
      {
        int cm, n;
        for (j=0; j<(CR_LIMIT+MAXPLAYERS); j++)
        {
          for (n=0; n<NUMCOLORMAPS+1; n++)
          {
            for (cm=0; cm<numcolormaps; cm++)
            {
              glDeleteTextures(1,&((*items)[i]->glTexExID[j][n][cm]));
            }
          }
        }
        Z_Free((*items)[i]->glTexExID);
        (*items)[i]->glTexExID = NULL;
      }
      else
      {
        for (j=0; j<(CR_LIMIT+MAXPLAYERS); j++)
          glDeleteTextures(1,&((*items)[i]->glTexID[j]));
      }

      Z_Free((*items)[i]);
    }
  }
  memset((*items),0,count*sizeof(GLTexture *));
}

void gld_Precache(void)
{
  register int i;
  register byte *hitlist;

  unsigned int tics = SDL_GetTicks();

  int usehires = gl_texture_usehires || gl_patch_usehires;

  if (doSkip)
    return;

  if (!usehires)
  {
    if (!precache)
      return;

    if (demoplayback)
      return;
  }

  if (gl_texture_usehires)
  {
    gld_ProgressStart();
  }

  {
    size_t size = numflats > numsprites  ? numflats : numsprites;
    hitlist = Z_Malloc((size_t)numtextures > size ? numtextures : size,PU_LEVEL,0);
  }

  // Precache flats.

  memset(hitlist, 0, numflats);

  for (i = numsectors; --i >= 0; )
  {
    int j,k;
    
    int floorpic = sectors[i].floorpic;
    int ceilingpic = sectors[i].ceilingpic;
    
    anim_t *flatanims[2] = {
      anim_flats[floorpic].anim,
      anim_flats[ceilingpic].anim
    };

    hitlist[floorpic] = hitlist[ceilingpic] = 1;
    
    //e6y: animated flats
    for (k = 0; k < 2; k++)
    {
      if (flatanims[k] && !flatanims[k]->istexture)
      {
        for (j = flatanims[k]->basepic; j < flatanims[k]->basepic + flatanims[k]->numpics; j++)
          hitlist[j] = 1;
      }
    }
  }

  for (i = numflats; --i >= 0; )
    if (hitlist[i])
    {
      GLTexture *gltexture = gld_RegisterFlat(i,true);
      gld_BindFlat(gltexture);
      if (gltexture && (gltexture->flags & GLTEXTURE_HIRES))
        gld_ProgressUpdate("Loading Flats...", numflats - i, numflats);
    }

  // Precache textures.

  memset(hitlist, 0, numtextures);

  for (i = numsides; --i >= 0;)
  {
    int j, k;
    
    int bottomtexture = sides[i].bottomtexture;
    int toptexture = sides[i].toptexture;
    int midtexture = sides[i].midtexture;
    
    anim_t *textureanims[3] = {
      anim_textures[bottomtexture].anim,
      anim_textures[toptexture].anim,
      anim_textures[midtexture].anim
    };

    hitlist[bottomtexture] =
      hitlist[toptexture] =
      hitlist[midtexture] = 1;

    //e6y: animated textures
    for (k = 0; k < 3; k++)
    {
      if (textureanims[k] && textureanims[k]->istexture)
      {
        for (j = textureanims[k]->basepic; j < textureanims[k]->basepic + textureanims[k]->numpics; j++)
          hitlist[j] = 1;
      }
    }

    //e6y: swithes
    {
      int GetPairForSwitchTexture(side_t *side);
      int pair = GetPairForSwitchTexture(&sides[i]);
      if (pair != -1)
        hitlist[pair] = 1;
    }
  }

  // Sky texture is always present.
  // Note that F_SKY1 is the name used to
  //  indicate a sky floor/ceiling as a flat,
  //  while the sky texture is stored like
  //  a wall texture, with an episode dependend
  //  name.

  if (hitlist)
    hitlist[skytexture] = gl_texture_usehires ? 1 : 0;

  for (i = numtextures; --i >= 0; )
    if (hitlist[i])
    {
      GLTexture *gltexture = gld_RegisterTexture(i,true,false);
      gld_BindTexture(gltexture);
      if (gltexture && (gltexture->flags & GLTEXTURE_HIRES))
        gld_ProgressUpdate("Loading Textures...", numtextures - i, numtextures);
    }

  // Precache sprites.
  memset(hitlist, 0, numsprites);

  if (hitlist)
  {
    thinker_t *th;
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
      if (th->function == P_MobjThinker)
        hitlist[((mobj_t *)th)->sprite] = 1;
  }

  for (i=numsprites; --i >= 0;)
    if (hitlist[i])
      {
        int j = sprites[i].numframes;
        while (--j >= 0)
          {
            short *sflump = sprites[i].spriteframes[j].lump;
            int k = 7;
            do
              gld_BindPatch(gld_RegisterPatch(firstspritelump + sflump[k],CR_DEFAULT),CR_DEFAULT);
            while (--k >= 0);
          }
      }
  Z_Free(hitlist);

  if (gl_texture_usehires)
  {
    gld_ProgressEnd();
  }

  // e6y: some statistics.  make sense for hires
  {
    char map[8];

    if (gamemode == commercial)
      sprintf(map, "MAP%02i", gamemap);
    else
      sprintf(map, "E%iM%i", gameepisode, gamemap);
    
    lprintf(LO_INFO, "gld_Precache: %s done in %d ms\n", map, SDL_GetTicks() - tics);
  }
}

void gld_CleanMemory(void)
{
  gld_CleanVertexData();
  gld_CleanTexItems(numtextures, &gld_GLTextures);
  gld_CleanTexItems(numlumps, &gld_GLPatchTextures);
}