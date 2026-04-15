// Engine/Renderer/Mesh.h
#pragma once
#include <d3d11.h>
#include <vector>
#include <wrl.h>

struct Vertex {
    float x, y, z;
    float u, v;       // Текстурные координаты
    float nx, ny, nz; // Нормали для освещения
};

class Mesh {
public:
    Mesh(ID3D11Device* device, const std::vector<Vertex>& vertices);
    void Bind(ID3D11DeviceContext* context);
    int GetVertexCount() const { return vertexCount; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
    int vertexCount;
};