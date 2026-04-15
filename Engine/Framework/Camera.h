// Engine/Framework/Camera.h
#include <directxmath.h>
using namespace DirectX;

class Camera {
public:
    XMMATRIX GetViewMatrix() {
        return XMMatrixLookAtLH(position, position + forward, up);
    }
    XMMATRIX GetProjectionMatrix(float aspect) {
        return XMMatrixPerspectiveFovLH(XMConvertToRadians(70.0f), aspect, 0.1f, 1000.0f);
    }
    // Здесь будут твои методы Move(), Rotate() для FPS
private:
    XMVECTOR position = { 0, 0, -5 };
    XMVECTOR forward = { 0, 0, 1 };
    XMVECTOR up = { 0, 1, 0 };
};