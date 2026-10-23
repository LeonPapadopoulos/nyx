// What a material draws with while its shader hasn't compiled, or when its file can't be read: a
// magenta and black checker, unlit, so it can't be taken for a look (VulkanMaterials)

import nyx.surface;

[shader("vertex")]
VertexOutput missingVertex(VertexInput input)
{
    return transformSurfaceVertex(input);
}

[shader("fragment")]
float4 missingFragment(VertexOutput vertex) : SV_Target
{
    const int2 cell = int2(floor(vertex.UV * 8.0));
    return ((cell.x + cell.y) & 1) == 0 ? float4(1.0, 0.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}

// As a decal in the editor's picking pass: all of the quad is the decal's, as the checker covers
// all of it (decalPickingFragment in nyx.surface)
[shader("fragment")]
uint missingPickingFragment(VertexOutput vertex) : SV_Target
{
    return Object.EntityId;
}
