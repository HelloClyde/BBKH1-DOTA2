"""Offline prototype: retain complete original alpha cards without QEM.

Returns source vertex IDs and source face IDs. XYZ, UV and normal data must be
gathered with those exact IDs; this module never moves or merges card vertices.
Only isolated two-triangle/four-vertex components qualify. Connected branches
or large alpha surfaces are deliberately excluded from this card-only path.
"""
from collections import Counter
from gltf_assets import np


def components(faces, vertex_count):
    faces = np.asarray(faces, dtype=np.int32)
    parent = np.arange(vertex_count, dtype=np.int32)

    def root(index):
        while parent[index] != index:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return int(index)

    for a, b, c in faces:
        a, b, c = root(a), root(b), root(c)
        parent[b] = a
        parent[c] = a
    groups = {}
    for index, face in enumerate(faces):
        groups.setdefault(root(face[0]), []).append(index)
    return [np.asarray(ids, dtype=np.int32) for ids in groups.values()]


def select_alpha_cards(points, faces, vertex_budget=648):
    points = np.asarray(points, dtype=float)
    faces = np.asarray(faces, dtype=np.int32)
    groups = components(faces, len(points))
    cards = []
    sizes = Counter()
    for ids in groups:
        vertices = np.unique(faces[ids])
        sizes[len(vertices), len(ids)] += 1
        if len(vertices) == 4 and len(ids) == 2:
            # A connected pair of valid triangles with one shared edge.
            if all(len(np.unique(face)) == 3 for face in faces[ids]):
                cards.append((ids, vertices))
    if not cards or vertex_budget < 4:
        raise ValueError('No independent original quad cards fit the vertex budget')
    count = min(len(cards), int(vertex_budget) // 4)
    centres = np.asarray([points[vertices].mean(axis=0) for _, vertices in cards])
    extent = np.maximum(np.ptp(centres, axis=0), 1e-9)
    normalized = (centres - centres.min(axis=0)) / extent
    # Deterministic farthest-point coverage in original 3D space. Start at the
    # highest crown card, then preserve distribution without altering geometry.
    first = int(np.argmax(centres[:, 1]))
    chosen, distance = [], np.full(len(cards), np.inf)
    candidate = first
    for _ in range(count):
        chosen.append(candidate)
        distance = np.minimum(distance, np.sum((normalized - normalized[candidate]) ** 2, axis=1))
        distance[chosen] = -1
        candidate = int(np.argmax(distance))
    face_ids = np.sort(np.concatenate([cards[i][0] for i in chosen]))
    vertex_ids, remapped = np.unique(faces[face_ids], return_inverse=True)
    report = {'component_count': len(groups), 'quad_card_count': len(cards),
              'selected_cards': count, 'vertices': len(vertex_ids), 'triangles': len(face_ids),
              'source_face_ids': face_ids.tolist(), 'source_vertex_ids': vertex_ids.tolist(),
              'component_sizes': [{'vertices': v, 'triangles': t, 'count': n}
                                  for (v, t), n in sorted(sizes.items())],
              'method': '3D farthest-point component selection; unchanged source XYZ/UV/normal IDs'}
    return vertex_ids, remapped.reshape(-1, 3), face_ids, report
