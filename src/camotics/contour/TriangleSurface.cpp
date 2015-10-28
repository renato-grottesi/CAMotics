/******************************************************************************\

    CAMotics is an Open-Source simulation and CAM software.
    Copyright (C) 2011-2015 Joseph Coffland <joseph@cauldrondevelopment.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

\******************************************************************************/

#include "TriangleSurface.h"

#include "TriangleMesh.h"

#include <cbang/Exception.h>
#include <cbang/log/Logger.h>

#include <camotics/Task.h>
#include <camotics/view/GL.h>
#include <camotics/stl/STLSource.h>
#include <camotics/stl/STLSink.h>

using namespace std;
using namespace cb;
using namespace CAMotics;


TriangleSurface::TriangleSurface(STLSource &source, Task *task) :
  finalized(false), useVBOs(true) {
  vbufs[0] = 0;
  read(source, task);
}


TriangleSurface::TriangleSurface(vector<SmartPointer<Surface> > &surfaces) :
  finalized(false), useVBOs(true) {
  vbufs[0] = 0;

  for (unsigned i = 0; i < surfaces.size(); i++) {
    TriangleSurface *s = dynamic_cast<TriangleSurface *>(surfaces[i].get());
    if (!s) THROW("Expected an TriangleSurface");

    // Copy surface data
    vertices.insert(vertices.end(), s->vertices.begin(), s->vertices.end());
    normals.insert(normals.end(), s->normals.begin(), s->normals.end());
    bounds.add(s->bounds);

    surfaces[i] = 0; // Free memory as we go
  }
}


TriangleSurface::TriangleSurface(const TriangleSurface &o) :
  TriangleMesh(o), finalized(false), bounds(o.bounds) {
  vbufs[0] = 0;
}


TriangleSurface::TriangleSurface() : finalized(false) {
  vbufs[0] = 0;
}


TriangleSurface::~TriangleSurface() {
  if (vbufs[0]) glDeleteBuffers(2, vbufs);
}


void TriangleSurface::finalize(bool withVBOs) {
  if (finalized) return;

  useVBOs = haveVBOs() && withVBOs;

  if (useVBOs) {
    if (!vbufs[0]) glGenBuffers(2, vbufs);

    // Vertices
    glBindBuffer(GL_ARRAY_BUFFER, vbufs[0]);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float),
                 &vertices[0], GL_STATIC_DRAW);

    // Normals
    glBindBuffer(GL_ARRAY_BUFFER, vbufs[1]);
    glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(float),
                 &normals[0], GL_STATIC_DRAW);
  }

  finalized = true;
}


void TriangleSurface::add(const Vector3R vertices[3]) {
  // Compute face normal
  Vector3R normal =
    (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);

  // Normalize
  real length = normal.length();
  if (length == 0) return; // Degenerate element, skip
  normal /= length;

  // Add it
  add(vertices, normal);
}


void TriangleSurface::add(const Vector3R vertices[3], const Vector3R &normal) {
  for (unsigned i = 0; i < 3; i++) {
    bounds.add(vertices[i]);

    for (unsigned j = 0; j < 3; j++) {
      this->vertices.push_back(vertices[i][j]);
      normals.push_back(normal[j]);
    }
  }
}


SmartPointer<Surface> TriangleSurface::copy() const {
  return new TriangleSurface(*this);
}


void TriangleSurface::draw(bool withVBOs) {
  if (!getCount()) return; // Nothing to draw

  finalize(withVBOs);

  if (useVBOs) {
    glBindBuffer(GL_ARRAY_BUFFER, vbufs[0]);
    glVertexPointer(3, GL_FLOAT, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, vbufs[1]);
    glNormalPointer(GL_FLOAT, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

  } else {
    glVertexPointer(3, GL_FLOAT, 0, &vertices[0]);
    glNormalPointer(GL_FLOAT, 0, &normals[0]);
  }

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);

  glDrawArrays(GL_TRIANGLES, 0, getCount() * 3);

  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);
}


void TriangleSurface::clear() {
  finalized = false;

  vertices.clear();
  normals.clear();

  bounds = Rectangle3R();
}


void TriangleSurface::read(STLSource &source, Task *task) {
  clear();

  uint32_t facets = source.getFacetCount(); // ASCII STL files return 0
  if (facets) {
    normals.reserve(9 * facets);
    vertices.reserve(9 * facets);
  }

  Vector3F v[3];
  Vector3F n;

  for (unsigned i = 0; source.hasMore() && (!task || !task->shouldQuit());
       i++) {
    // Read facet
    source.readFacet(v[0], v[1], v[2], n);

    // Validate
    bool valid = n.isReal();
    for (unsigned j = 0; j < 3; j++)
      if (!v[j].isReal()) valid = false;

    if (!valid) {
      LOG_ERROR("Invalid facet in STL: normal=" << n << " triangle=("
                << v[0] << ", " << v[1] << ", " << v[2] << ")");
      continue;
    }

    // Normals
    for (unsigned j = 0; j < 3; j++)
      for (unsigned k = 0; k < 3; k++)
        normals.push_back(n[k]); // STL has only a face normal

    // Vertices
    for (unsigned j = 0; j < 3; j++) {
      for (unsigned k = 0; k < 3; k++)
        vertices.push_back(v[j][k]);

      bounds.add(v[j]);
    }

    if (task) {
      if (!facets && 100000 < i) i = 0;
      task->update((double)i / (facets ? facets : 100000),
                   "Reading STL surface");
    }
  }

  task->update(1, "Idle");
}


void TriangleSurface::write(STLSink &sink, Task *task) const {
  Vector3R p[3];

  for (unsigned i = 0; i < getCount() && (!task || !task->shouldQuit()); i++) {
    unsigned offset = i * 9;

    // The vertex normals are all the same
    Vector3R normal =
      Vector3R(normals[offset + 0], normals[offset + 1], normals[offset + 2]);

    for (unsigned j = 0; j < 3; j++)
      for (unsigned k = 0; k < 3; k++)
        p[j][k] = vertices[offset + j * 3 + k];

    sink.writeFacet(p[0], p[1], p[2], normal);

    if (task) task->update((double)i / getCount(), "Writing STL surface");
  }
}


void TriangleSurface::reduce(Task &task) {
  weld();
  TriangleMesh::reduce(task);
}