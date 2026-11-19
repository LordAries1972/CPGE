//-------------------------------------------------------------------------------------------------
// Vectors.cpp - MyVector / MyQuaternion / MyMath / MyGeom implementation
//
// Every trigonometric call in this file goes through the MathPrecalculation lookup tables
// (FAST_MATH) rather than the C runtime.  Table facts that shaped the code below:
//   * sin/cos : 65536 entries over [0, 2*PI]  -> ~1e-4 rad resolution, plenty for rendering.
//   * FastACos is coarse near +-1 (a ~0.6 degree step for nearly parallel vectors), so angle
//     maths uses FastACosAccurate / FastSinCosDeg - two tables added to MathPrecalculation for
//     this file (interpolated, accurate everywhere, degree-domain input).
//   * Each lookup bumps an atomic statistics counter, so hot loops hoist ONE SinCosDeg lookup
//     out of the loop (see RotateAroundBatch) instead of calling Sin/Cos per element.
//-------------------------------------------------------------------------------------------------

#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "Vectors.h"
#include "MathPrecalculation.h"          // FAST_MATH lookup tables (FastSinCosDeg / FastACosAccurate ... travel with this file when merging)

#include <sstream>
#include <fstream>

//==================================================================================================
// MyMath - table driven trigonometry
//==================================================================================================
namespace MyMath
{
    float Sin(float radians)                              { return FAST_MATH.FastSin(radians); }
    float Cos(float radians)                              { return FAST_MATH.FastCos(radians); }
    void  SinCos(float radians, float& s, float& c)       { FAST_MATH.FastSinCos(radians, s, c); }   // single lookup
    void  SinCosDeg(float degrees, float& s, float& c)    { FAST_MATH.FastSinCosDeg(degrees, s, c); }   // degrees in, interpolated
    float Asin(float v)                                   { return FAST_MATH.FastASin(v); }
    float Acos(float v)                                   { return FAST_MATH.FastACosAccurate(v); }      // accurate near +-1
    float Atan2(float y, float x)                         { return FAST_MATH.FastATan2(y, x); }

    // Mathf.SmoothDamp - critically damped spring with a cubic approximation of exp(-omega*dt)
    // (no exp() call, so it is cheap enough to run per entity per frame).
    float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime,
                     float maxSpeed, float deltaTime)
    {
        smoothTime = Max(0.0001f, smoothTime);
        const float omega = 2.0f / smoothTime;
        const float x = omega * deltaTime;
        const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        float change = current - target;
        const float originalTo = target;
        const float maxChange = maxSpeed * smoothTime;
        change = Clamp(change, -maxChange, maxChange);                  // limit the top speed
        target = current - change;

        const float temp = (currentVelocity + omega * change) * deltaTime;
        currentVelocity = (currentVelocity - omega * temp) * decay;
        float output = target + (change + temp) * decay;

        // Prevent overshooting the original target.
        if ((originalTo - current > 0.0f) == (output > originalTo))
        {
            output = originalTo;
            currentVelocity = (output - originalTo) / deltaTime;
        }
        return output;
    }
}

//==================================================================================================
// MyVector
//==================================================================================================
namespace
{
    // Slerp between two UNIT directions.  Falls back to a normalised lerp for (anti)parallel input,
    // where sin(theta) -> 0 would otherwise explode.
    MyVector SlerpUnitDirections(const MyVector& a, const MyVector& b, float t)
    {
        const float dot = MyMath::Clamp(a.Dot(b), -1.0f, 1.0f);

        if (dot > 0.999999f)                                            // practically identical
            return MyVector::LerpUnclamped(a, b, t).Normalized();

        if (dot < -0.999999f)                                           // opposite: any perpendicular axis works
        {
            MyVector axis = a.Cross(MyVector::Up());
            if (axis.SqrMagnitude() < 1e-6f) axis = a.Cross(MyVector::Right());
            return a.RotatedAround(axis.Normalized(), 180.0f * t);
        }

        const float theta = MyMath::Acos(dot);                          // angle between the directions (accurate table)
        const float invSinTheta = 1.0f / MyMath::Sin(theta);
        const float wa = MyMath::Sin((1.0f - t) * theta) * invSinTheta;
        const float wb = MyMath::Sin(t * theta) * invSinTheta;
        return a * wa + b * wb;
    }
}

MyVector MyVector::SmoothDamp(const MyVector& current, const MyVector& target, MyVector& currentVelocity,
                              float smoothTime, float maxSpeed, float deltaTime)
{
    smoothTime = MyMath::Max(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * deltaTime;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    MyVector change = current - target;
    const MyVector originalTo = target;

    // Clamp the maximum speed of the spring.
    const float maxChange = maxSpeed * smoothTime;
    const float maxChangeSq = maxChange * maxChange;
    const float sq = change.SqrMagnitude();
    if (sq > maxChangeSq) change = change * (maxChange / std::sqrt(sq));

    const MyVector clampedTarget = current - change;
    const MyVector temp = (currentVelocity + change * omega) * deltaTime;
    currentVelocity = (currentVelocity - temp * omega) * decay;
    MyVector output = clampedTarget + (change + temp) * decay;

    // Prevent overshoot.
    if ((originalTo - current).Dot(output - originalTo) > 0.0f)
    {
        output = originalTo;
        currentVelocity = (output - originalTo) / deltaTime;
    }
    return output;
}

float MyVector::Angle(const MyVector& from, const MyVector& to)
{
    // Vector3.Angle: guard against zero length, then angle between directions in degrees.
    const float denom = std::sqrt(from.SqrMagnitude() * to.SqrMagnitude());
    if (denom < MyMath::kEpsilonNormalSqrt) return 0.0f;
    return MyMath::Acos(from.Dot(to) / denom) * MyMath::kRad2Deg;      // Acos clamps, so rounding past +-1 is safe
}

float MyVector::SignedAngle(const MyVector& from, const MyVector& to, const MyVector& axis)
{
    const float unsignedAngle = Angle(from, to);
    const float sign = MyMath::Sign(axis.Dot(from.Cross(to)));
    return unsignedAngle * sign;
}

float MyVector::AngleBetween(const MyVector& from, const MyVector& to)
{
    return Angle(from, to) * MyMath::kDeg2Rad;
}

float MyVector::SignedVectorAngle(const MyVector& reference, const MyVector& other, const MyVector& normal)
{
    // FORGE3D.F3DMath.SignedVectorAngle: Angle(reference, other) * Sign(Dot(Cross(normal, reference), other)).
    const MyVector lhs = normal.Cross(reference);
    return Angle(reference, other) * MyMath::Sign(lhs.Dot(other));
}

MyVector MyVector::SlerpUnclamped(const MyVector& a, const MyVector& b, float t)
{
    const float la = a.Magnitude();
    const float lb = b.Magnitude();
    if (la < MyMath::kFloatMin || lb < MyMath::kFloatMin)               // a zero vector has no direction to slerp
        return LerpUnclamped(a, b, t);

    const MyVector dir = SlerpUnitDirections(a / la, b / lb, t);        // direction slerps, length lerps
    return dir * MyMath::LerpUnclamped(la, lb, t);
}

MyVector MyVector::Slerp(const MyVector& a, const MyVector& b, float t)
{
    return SlerpUnclamped(a, b, MyMath::Clamp01(t));
}

MyVector MyVector::RotateTowards(const MyVector& current, const MyVector& target,
                                 float maxRadiansDelta, float maxMagnitudeDelta)
{
    const float lc = current.Magnitude();
    const float lt = target.Magnitude();
    if (lc < MyMath::kFloatMin || lt < MyMath::kFloatMin)
        return MoveTowards(current, target, maxMagnitudeDelta);

    const MyVector dc = current / lc;
    const MyVector dt = target / lt;
    const float newLength = MyMath::MoveTowards(lc, lt, maxMagnitudeDelta);
    const float angle = MyMath::Acos(dc.Dot(dt));                       // radians between directions

    if (angle < 1e-6f) return dt * newLength;                           // already facing the target
    const float t = MyMath::Min(1.0f, maxRadiansDelta / angle);         // never rotate past the target
    return SlerpUnitDirections(dc, dt, t) * newLength;
}

void MyVector::OrthoNormalize(MyVector& normal, MyVector& tangent)
{
    normal.Normalize();
    tangent = (tangent - Project(tangent, normal)).Normalized();        // strip the normal component, renormalise
}

void MyVector::OrthoNormalize(MyVector& normal, MyVector& tangent, MyVector& binormal)
{
    normal.Normalize();
    tangent = (tangent - Project(tangent, normal)).Normalized();
    binormal = binormal - Project(binormal, normal) - Project(binormal, tangent);
    binormal.Normalize();
}

// ---- rotation of a single vector ----------------------------------------------------------------
MyVector MyVector::RotatedX(float degrees) const
{
    float s, c;
    MyMath::SinCosDeg(degrees, s, c);                                   // one lookup yields both, no radian conversion
    return MyVector(x, y * c - z * s, y * s + z * c);
}

MyVector MyVector::RotatedY(float degrees) const
{
    float s, c;
    MyMath::SinCosDeg(degrees, s, c);
    return MyVector(x * c + z * s, y, -x * s + z * c);
}

MyVector MyVector::RotatedZ(float degrees) const
{
    float s, c;
    MyMath::SinCosDeg(degrees, s, c);
    return MyVector(x * c - y * s, x * s + y * c, z);
}

MyVector MyVector::RotatedAround(const MyVector& axis, float degrees) const
{
    // Rodrigues: v' = v*cos + (k x v)*sin + k*(k.v)*(1-cos), k = unit axis.
    const MyVector k = axis.Normalized();
    if (k.SqrMagnitude() == 0.0f) return *this;                         // degenerate axis: no rotation
    float s, c;
    MyMath::SinCosDeg(degrees, s, c);
    return (*this) * c + k.Cross(*this) * s + k * (k.Dot(*this) * (1.0f - c));
}

MyVector MyVector::Rotated(const MyQuaternion& q) const
{
    return q * (*this);
}

void MyVector::RotateAroundBatch(MyVector* v, size_t count, const MyVector& axis, float degrees)
{
    const MyVector k = axis.Normalized();
    if (!v || count == 0 || k.SqrMagnitude() == 0.0f) return;

    // The ONE table lookup for the whole batch; the loop below is pure multiply-add.
    float s, c;
    MyMath::SinCosDeg(degrees, s, c);
    const float oneMinusC = 1.0f - c;

    for (size_t i = 0; i < count; ++i)
    {
        const MyVector p = v[i];
        v[i] = p * c + k.Cross(p) * s + k * (k.Dot(p) * oneMinusC);
    }
}

// ---- game helpers recovered from MathUtil ------------------------------------------------------
float MyVector::DirectionTo2dDeg(float px, float py)
{
    // MathUtil.DirectionTo2dDeg: (degrees(atan2(-x, y)) + 360) % 360
    return std::fmod(MyMath::Atan2(-px, py) * MyMath::kRad2Deg + 360.0f, 360.0f);
}

MyVector MyVector::ReflectionDirectionFromAngle(float a)
{
    // MathUtil.GetReflectionDirectionFromAngle - quantises a heading to one of the 4 wall normals.
    if (a > 45.0f  && a < 135.0f) return Left();
    if (a >= 135.0f && a < 225.0f) return Down();
    if (a >= 225.0f && a <= 315.0f) return Right();
    return Up();
}

MyVector MyVector::LineDirectionFromAngle(float a)
{
    // MathUtil.GetLineDirectionFromAngle - same idea with narrow left/right and wide up/down cones.
    if (a > 65.0f  && a < 115.0f) return Left();
    if (a >= 115.0f && a < 245.0f) return Down();
    if (a >= 245.0f && a <= 295.0f) return Right();
    return Up();
}

MyVector MyVector::NormalFromVelocity(const MyVector& direction)
{
    return ReflectionDirectionFromAngle(DirectionTo2dDeg(direction.x, direction.y));
}

// ---- text / persistence ------------------------------------------------------------------------
std::string MyVector::ToString() const
{
    std::ostringstream oss;
    oss << "MyVector(" << x << ", " << y << ", " << z << ")";
    return oss.str();
}

std::string MyVector::Serialize() const
{
    std::ostringstream oss;
    oss << x << "," << y << "," << z;
    return oss.str();
}

MyVector MyVector::Deserialize(const std::string& s)
{
    std::istringstream iss(s);
    float px, py, pz;
    char c1, c2;
    if (iss >> px >> c1 >> py >> c2 >> pz && c1 == ',' && c2 == ',')
        return MyVector(px, py, pz);
    return MyVector();                                                  // malformed input -> zero vector
}

size_t MyVector::GetHashCode() const
{
    // Unity's recipe: x ^ (y << 2) ^ (z >> 2), using the float hashes.
    const std::hash<float> h;
    return h(x) ^ (h(y) << 2) ^ (h(z) >> 2);
}

//==================================================================================================
// MyQuaternion
//==================================================================================================
MyQuaternion MyQuaternion::Inverse(const MyQuaternion& q)
{
    const float sq = Dot(q, q);
    if (sq < MyMath::kFloatMin) return Identity();
    const float inv = 1.0f / sq;
    return MyQuaternion(-q.x * inv, -q.y * inv, -q.z * inv, q.w * inv);
}

void MyQuaternion::Normalize()
{
    const float mag = std::sqrt(Dot(*this, *this));
    if (mag < MyMath::kFloatMin) { *this = Identity(); return; }
    const float inv = 1.0f / mag;
    x *= inv; y *= inv; z *= inv; w *= inv;
}

MyQuaternion MyQuaternion::AngleAxis(float degrees, const MyVector& axis)
{
    const MyVector k = axis.Normalized();
    if (k.SqrMagnitude() == 0.0f) return Identity();
    float s, c;
    MyMath::SinCosDeg(degrees * 0.5f, s, c);                            // half angle, single lookup
    return MyQuaternion(k.x * s, k.y * s, k.z * s, c);
}

MyQuaternion MyQuaternion::Euler(float xDeg, float yDeg, float zDeg)
{
    // Unity order: rotate about Z, then X, then Y  ==  qY * qX * qZ.
    const MyQuaternion qx = AngleAxis(xDeg, MyVector::Right());
    const MyQuaternion qy = AngleAxis(yDeg, MyVector::Up());
    const MyQuaternion qz = AngleAxis(zDeg, MyVector::Forward());
    return qy * qx * qz;
}

MyQuaternion MyQuaternion::LookRotation(const MyVector& forward, const MyVector& up)
{
    const MyVector f = forward.Normalized();
    if (f.SqrMagnitude() == 0.0f) return Identity();

    MyVector r = up.Cross(f);                                           // left-handed basis, like Unity
    if (r.SqrMagnitude() < 1e-12f)                                      // up parallel to forward: pick another reference
        r = MyVector::Right().Cross(f);
    r.Normalize();
    const MyVector u = f.Cross(r);

    // Rotation matrix with columns (right, up, forward) -> quaternion (Shepperd's method).
    const float m00 = r.x, m01 = u.x, m02 = f.x;
    const float m10 = r.y, m11 = u.y, m12 = f.y;
    const float m20 = r.z, m21 = u.z, m22 = f.z;
    const float trace = m00 + m11 + m22;
    MyQuaternion q;
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s; q.x = (m21 - m12) / s; q.y = (m02 - m20) / s; q.z = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s; q.x = 0.25f * s; q.y = (m01 + m10) / s; q.z = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s; q.x = (m01 + m10) / s; q.y = 0.25f * s; q.z = (m12 + m21) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s; q.x = (m02 + m20) / s; q.y = (m12 + m21) / s; q.z = 0.25f * s;
    }
    q.Normalize();
    return q;
}

MyQuaternion MyQuaternion::FromToRotation(const MyVector& from, const MyVector& to)
{
    // Half-vector construction: q = (a x b, 1 + a.b), normalised.  No trig at all.
    const MyVector a = from.Normalized();
    const MyVector b = to.Normalized();
    if (a.SqrMagnitude() == 0.0f || b.SqrMagnitude() == 0.0f) return Identity();

    const float d = a.Dot(b);
    if (d > 0.999999f) return Identity();                               // already aligned
    if (d < -0.999999f)                                                 // opposite: 180 degrees about any perpendicular
    {
        MyVector axis = a.Cross(MyVector::Up());
        if (axis.SqrMagnitude() < 1e-6f) axis = a.Cross(MyVector::Right());
        axis.Normalize();
        return MyQuaternion(axis.x, axis.y, axis.z, 0.0f);
    }
    const MyVector c = a.Cross(b);
    MyQuaternion q(c.x, c.y, c.z, 1.0f + d);
    q.Normalize();
    return q;
}

MyVector MyQuaternion::EulerAngles() const
{
    // Inverse of Euler(): derived from R = Ry(y) * Rx(x) * Rz(z).
    const MyQuaternion q = Normalized();
    const float sinX = MyMath::Clamp(2.0f * (q.w * q.x - q.y * q.z), -1.0f, 1.0f);
    float ex, ey, ez;
    ex = MyMath::Asin(sinX);
    if (MyMath::Abs(sinX) < 0.9999f)
    {
        ey = MyMath::Atan2(2.0f * (q.x * q.z + q.w * q.y), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        ez = MyMath::Atan2(2.0f * (q.x * q.y + q.w * q.z), 1.0f - 2.0f * (q.x * q.x + q.z * q.z));
    }
    else                                                                // gimbal lock: fold roll into yaw
    {
        ey = MyMath::Atan2(-2.0f * (q.x * q.z - q.w * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
        ez = 0.0f;
    }
    MyVector deg(ex * MyMath::kRad2Deg, ey * MyMath::kRad2Deg, ez * MyMath::kRad2Deg);
    deg.x = MyMath::Repeat(deg.x, 360.0f);                              // Unity reports [0,360)
    deg.y = MyMath::Repeat(deg.y, 360.0f);
    deg.z = MyMath::Repeat(deg.z, 360.0f);
    return deg;
}

void MyQuaternion::ToAngleAxis(float& outDegrees, MyVector& outAxis) const
{
    const MyQuaternion q = Normalized();
    outDegrees = 2.0f * MyMath::Acos(MyMath::Clamp(q.w, -1.0f, 1.0f)) * MyMath::kRad2Deg;
    const float s = std::sqrt(MyMath::Max(0.0f, 1.0f - q.w * q.w));
    outAxis = (s < 1e-6f) ? MyVector::Right() : MyVector(q.x / s, q.y / s, q.z / s);
}

float MyQuaternion::Angle(const MyQuaternion& a, const MyQuaternion& b)
{
    const float d = MyMath::Min(MyMath::Abs(Dot(a, b)), 1.0f);          // |dot|: q and -q are the same rotation
    return 2.0f * MyMath::Acos(d) * MyMath::kRad2Deg;
}

MyQuaternion MyQuaternion::LerpUnclamped(const MyQuaternion& a, const MyQuaternion& b, float t)
{
    // Take the short way round: flip b when the quaternions point into opposite hemispheres.
    const float sign = (Dot(a, b) < 0.0f) ? -1.0f : 1.0f;
    MyQuaternion r(a.x + (sign * b.x - a.x) * t, a.y + (sign * b.y - a.y) * t,
                   a.z + (sign * b.z - a.z) * t, a.w + (sign * b.w - a.w) * t);
    r.Normalize();
    return r;
}

MyQuaternion MyQuaternion::Lerp(const MyQuaternion& a, const MyQuaternion& b, float t)
{
    return LerpUnclamped(a, b, MyMath::Clamp01(t));
}

MyQuaternion MyQuaternion::SlerpUnclamped(const MyQuaternion& a, const MyQuaternion& b, float t)
{
    float d = Dot(a, b);
    MyQuaternion bb = b;
    if (d < 0.0f) { d = -d; bb = MyQuaternion(-b.x, -b.y, -b.z, -b.w); }   // shortest arc

    if (d > 0.9995f) return LerpUnclamped(a, bb, t);                    // nearly equal: sin(theta) ~ 0, nlerp is exact enough

    const float theta = MyMath::Acos(d);
    const float invSin = 1.0f / MyMath::Sin(theta);
    const float wa = MyMath::Sin((1.0f - t) * theta) * invSin;
    const float wb = MyMath::Sin(t * theta) * invSin;
    MyQuaternion r(a.x * wa + bb.x * wb, a.y * wa + bb.y * wb, a.z * wa + bb.z * wb, a.w * wa + bb.w * wb);
    r.Normalize();
    return r;
}

MyQuaternion MyQuaternion::Slerp(const MyQuaternion& a, const MyQuaternion& b, float t)
{
    return SlerpUnclamped(a, b, MyMath::Clamp01(t));
}

MyQuaternion MyQuaternion::RotateTowards(const MyQuaternion& from, const MyQuaternion& to, float maxDegreesDelta)
{
    const float angle = Angle(from, to);
    if (angle < 1e-4f) return to;
    return SlerpUnclamped(from, to, MyMath::Min(1.0f, maxDegreesDelta / angle));
}

std::string MyQuaternion::ToString() const
{
    std::ostringstream oss;
    oss << "MyQuaternion(" << x << ", " << y << ", " << z << ", " << w << ")";
    return oss.str();
}

//==================================================================================================
// Quality scaling of particle counts
//==================================================================================================
namespace MyMath
{
    // count * density with STOCHASTIC rounding: the fractional part is the chance of one extra particle.  A call that creates a single particle (a smoke
    // trail, a spark every few frames) at density 0.4 therefore spawns one 40% of the time, so trails thin out on average; round-to-nearest would give
    // 0 or 1 every single time.  A call for two or more particles never drops to none, so a blast always leaves something.  r01 is a random number in [0,1).
    int ScaleCount(int count, float density, float r01)
    {
        if (count <= 0) return 0;
        const float v = static_cast<float>(count) * density;
        int n = static_cast<int>(v);
        if (r01 < v - static_cast<float>(n)) ++n;
        return (n < 1 && count >= 2) ? 1 : n;
    }
}

//==================================================================================================
// MyGeom - stateless geometry helpers (moved from the vector demo in main.cpp)
//==================================================================================================
namespace MyGeom
{
    MyQuaternion FaceAlongX(const MyVector& dir)
    {
        if (dir.SqrMagnitude() < 1e-8f) return MyQuaternion::Identity();
        // LookRotation points +Z along dir; the extra -90 degrees about Up turns a +X-nosed mesh instead.
        return MyQuaternion::LookRotation(dir, MyVector::Up()) * MyQuaternion::AngleAxis(-90.0f, MyVector::Up());
    }

    bool BounceBlock(MyVector& pos, MyVector& vel, float r, const Rect2& inner, float restitution)
    {
        // Penetration depth into the block grown by r, per side.
        const float pL = pos.x - (inner.x0 - r), pR = (inner.x1 + r) - pos.x;
        const float pB = pos.z - (inner.y0 - r), pT = (inner.y1 + r) - pos.z;
        if (!(pL > 0.0f && pR > 0.0f && pB > 0.0f && pT > 0.0f)) return false;          // not inside the block grown by r
        bool hit = false;
        auto bounce = [&](const MyVector& n)
        {
            if (vel.Dot(n) < 0.0f) { vel = MyVector::Reflect(vel, n) * restitution; hit = true; }       // only when heading into the block
        };
        const float m = MyMath::Min(MyMath::Min(pL, pR), MyMath::Min(pB, pT));
        if      (m == pL) { pos.x = inner.x0 - r; bounce(MyVector::Left()); }
        else if (m == pR) { pos.x = inner.x1 + r; bounce(MyVector::Right()); }
        else if (m == pB) { pos.z = inner.y0 - r; bounce(MyVector::Back()); }
        else              { pos.z = inner.y1 + r; bounce(MyVector::Forward()); }
        return hit;
    }

    bool BounceArena(MyVector& pos, MyVector& vel, float r, const Rect2& outer, const Rect2& inner, float restitution)
    {
        bool hit = false;
        auto bounce = [&](const MyVector& n)
        {
            if (vel.Dot(n) < 0.0f) { vel = MyVector::Reflect(vel, n) * restitution; hit = true; }       // only when heading into the wall
        };
        if (pos.x < outer.x0 + r) { pos.x = outer.x0 + r; bounce(MyVector::Right()); }
        if (pos.x > outer.x1 - r) { pos.x = outer.x1 - r; bounce(MyVector::Left()); }
        if (pos.z < outer.y0 + r) { pos.z = outer.y0 + r; bounce(MyVector::Forward()); }
        if (pos.z > outer.y1 - r) { pos.z = outer.y1 - r; bounce(MyVector::Back()); }
        if (BounceBlock(pos, vel, r, inner, restitution)) hit = true;
        return hit;
    }
}

//==================================================================================================
// MyCubeExplosion
//==================================================================================================
MyCubeExplosion::MyCubeExplosion(size_t capacity, uint32_t seed)
    : m_pool(capacity > 0 ? capacity : 1), m_rng(seed ? seed : 1u)     // the only allocation: the whole pool up front
{
}

float MyCubeExplosion::Rand01()
{
    m_rng = m_rng * 1664525u + 1013904223u;                            // LCG: tiny, deterministic, no heap
    return static_cast<float>((m_rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

MyVector MyCubeExplosion::RandomUnit()
{
    // A random direction on the sphere is ONE load from the precalculated Fibonacci-spiral table (MathPrecalculation):
    // no sin / cos, no sqrt.  The generator is stepped here so every call picks an independent entry.
    m_rng = m_rng * 1664525u + 1013904223u;
    return MyVector(FAST_UNIT_SPHERE(m_rng));
}

void MyCubeExplosion::Spawn(const MyVector& origin, int count, float speed, float size, float lifetime,
                            uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity)
{
    count = MyMath::ScaleCount(count, m_density, Rand01());             // quality: fewer particles per call (stochastic rounding)
    for (int i = 0; i < count; ++i)
    {
        MyCubeParticle& p = m_pool[m_next];
        m_next = (m_next + 1) % m_pool.size();                         // ring buffer: recycle the oldest slot when full
        const bool wasFree = (p.life <= 0.0f);

        p.pos      = origin;
        p.vel      = RandomUnit() * (speed * Rand(0.35f, 1.0f)) + inheritVelocity;
        p.spinAxis = RandomUnit();                                     // every cube tumbles about its own axis
        p.spinRate = Rand(240.0f, 900.0f);                             // several full turns during its life
        p.orient   = MyQuaternion::AngleAxis(Rand(0.0f, 360.0f), RandomUnit());
        p.maxLife  = lifetime * Rand(0.6f, 1.0f);
        p.life     = p.maxLife;
        p.size     = size * Rand(0.5f, 1.0f);

        const float tint = Rand(0.7f, 1.0f);                           // per-cube brightness jitter
        p.r = static_cast<uint8_t>(r * tint);
        p.g = static_cast<uint8_t>(g * tint);
        p.b = static_cast<uint8_t>(b * tint);

        if (wasFree) ++m_alive;
    }
}

void MyCubeExplosion::Update(float dt, const MyVector& gravity, float drag)
{
    const float keep = MyMath::Max(0.0f, 1.0f - drag * dt);            // velocity retained this frame
    for (MyCubeParticle& p : m_pool)
    {
        if (p.life <= 0.0f) continue;
        p.life -= dt;
        if (p.life <= 0.0f) { p.life = 0.0f; --m_alive; continue; }

        p.pos += p.vel * dt;
        p.vel  = (p.vel + gravity * dt) * keep;
        p.orient = (MyQuaternion::AngleAxis(p.spinRate * dt, p.spinAxis) * p.orient).Normalized();
    }
}

void MyCubeExplosion::Clear()
{
    for (MyCubeParticle& p : m_pool) p.life = 0.0f;
    m_alive = 0;
}

// ================================================================================================
//  MyHighScores
// ================================================================================================
void MyHighScores::SetFile(const std::filesystem::path& file)
{
    if (!m_file.empty()) return;                           // already bound: keep the first file
    m_file = file;
    for (int& w : m_bestWave) w = 1;
    Load();
    PruneRecordings();
}

int MyHighScores::Insert(int game, int score, int wave, const char* name, bool demo, double seconds, uint64_t replay, double raceSeconds, int raceContext)
{
    if (score <= 0) return -1;
    Entry* t = m_top[Clamp(game)];
    int pos = kTopCount;
    while (pos > 0 && score > t[pos - 1].score) --pos;     // equal scores keep their seniority
    if (pos >= kTopCount) return -1;                       // not good enough for the table
    const uint64_t displaced = t[kTopCount - 1].replay;
    for (int i = kTopCount - 1; i > pos; --i) t[i] = t[i - 1];
    t[pos] = Entry(); t[pos].seconds = (std::max)(0.0, seconds); t[pos].replay = replay;
    t[pos].raceSeconds = raceSeconds; t[pos].raceContext = raceContext;
    t[pos].score = score; t[pos].wave = wave < 1 ? 1 : wave; t[pos].demo = demo;
    int n = 0;
    for (; name && name[n] && n < kNameLen; ++n) t[pos].name[n] = (name[n] == ' ') ? '_' : name[n];   // no spaces: the file is token based
    if (n == 0) { const char* d = "PLAYER"; for (; d[n]; ++n) t[pos].name[n] = d[n]; }
    t[pos].name[n] = 0;
    DiscardRecording(displaced);
    return pos;
}

bool MyHighScores::RecordingReferenced(uint64_t id) const
{
    if (!id) return false;
    if (m_pending && m_pendingReplay == id) return true;
    for (const auto& table : m_top) for (const Entry& entry : table)
        if (entry.score > 0 && entry.replay == id) return true;
    for (const auto& game : m_times) for (const auto& table : game) for (const Entry& entry : table.entries)
        if (entry.raceSeconds > 0.0 && entry.replay == id) return true;
    return false;
}

std::filesystem::path MyHighScores::RecordingPath(uint64_t id) const
{
    // Only a numeric identifier contributes to a filename. Never accept a
    // persisted path or derive deletion targets from a rank or player name.
    return m_file.parent_path() / "Playbacks" / (std::to_string(id) + ".mp4");
}

// STATE FILES.  Each recorded run also owns a dated object-state file  Playbacks/<scene>-<YYYY-MM-DD_HH-MM-SS>-<id>.cpr  (see ArcadeStateRecord.inl).  It shares the video's
// identity and its fate: kept only while a high-score entry, a pending name entry or the run in progress references that id.  Only a numeric id after the last '-' of a .cpr name is ever
// used to select a deletion target.
static bool StateFileId(const std::filesystem::path& file, uint64_t& id)
{
    if (file.extension() != ".cpr") return false;
    const std::string stem = file.stem().string(); const size_t dash = stem.find_last_of('-');
    if (dash == std::string::npos || dash + 1 >= stem.size() || stem.size() - dash - 1 > 19) return false;
    uint64_t value = 0;
    for (size_t i = dash + 1; i < stem.size(); ++i) { if (stem[i] < '0' || stem[i] > '9') return false; value = value * 10 + static_cast<unsigned>(stem[i] - '0'); }
    id = value; return value != 0;
}
static void RemoveStateFiles(const std::filesystem::path& directory, uint64_t id)
{
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        uint64_t found = 0;
        if (StateFileId(it->path(), found) && found == id) { std::error_code removeError; std::filesystem::remove(it->path(), removeError); }
    }
}

void MyHighScores::DiscardRecording(uint64_t id) const
{
    if (!id || id == m_runReplay || RecordingReferenced(id) || m_file.empty()) return;
    std::error_code error;
    RemoveStateFiles(RecordingPath(id).parent_path(), id);
    std::filesystem::remove(RecordingPath(id), error);
    auto manifest=RecordingPath(id); manifest.replace_extension(".json");
    std::filesystem::remove(manifest,error);
}

void MyHighScores::ClearRecording(uint64_t id)
{
    if (!id) return;
    for (auto& table : m_top) for (Entry& entry : table) if (entry.replay == id) entry.replay = 0;
    for (auto& game : m_times) for (auto& table : game) for (Entry& entry : table.entries) if (entry.replay == id) entry.replay = 0;
    for (Entry& entry : m_demoBest) if (entry.replay == id) entry.replay = 0;
    if (m_pendingReplay == id) m_pendingReplay = 0;
    if (m_runReplay == id) m_runReplay = 0;
    DiscardRecording(id); m_dirty = true;
}

void MyHighScores::ReportRaceTime(double seconds, int context)
{
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return;
    bool found = false;
    for (RunTime& time : m_runTimes) if (time.context == context) { time.seconds = (std::min)(time.seconds, seconds); found = true; break; }
    if (!found && m_runTimes.size() < kMaxTimeContexts) m_runTimes.push_back({context, seconds});
    // Retain the exact track/laps/car context of this run's fastest race.
    if (m_raceSeconds <= 0.0 || seconds < m_raceSeconds) { m_raceSeconds = seconds; m_raceContext = context; }
}

void MyHighScores::PruneRecordings() const
{
    if (m_file.empty()) return;
    std::error_code error;
    const auto directory = RecordingPath(1).parent_path();
    // Build references once, rather than rescanning every time table for each
    // file. Startup cleanup remains linear in retained entries and files.
    std::unordered_set<uint64_t> retained;
    for (const auto& table : m_top) for (const Entry& entry : table) if (entry.replay) retained.insert(entry.replay);
    for (const auto& game : m_times) for (const auto& table : game) for (const Entry& entry : table.entries) if (entry.replay) retained.insert(entry.replay);
    if (m_runReplay) retained.insert(m_runReplay);
    if (m_pending && m_pendingReplay) retained.insert(m_pendingReplay);
    // MANDATORY CLEAN-UP: this runs every time the table is bound at start-up (SetFile). Video clips, sidecars and state files whose id no high score references are deleted, so only
    // runs linked to a score survive; unrelated files are never touched.
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        uint64_t stateId = 0;
        if (StateFileId(it->path(), stateId)) {
            if (retained.find(stateId) == retained.end()) { std::error_code removeError; std::filesystem::remove(it->path(), removeError); }
            continue;
        }
        if (it->path().extension() != ".mp4" && it->path().extension() != ".json") continue;
        const std::string stem = it->path().stem().string();
        if (stem.empty() || stem.size() > 19 || stem.find_first_not_of("0123456789") != std::string::npos) continue;
        uint64_t id = 0; for (char ch : stem) id = id * 10 + static_cast<unsigned>(ch - '0');
        if (id && retained.find(id) == retained.end()) {
            std::error_code removeError;
            std::filesystem::remove(RecordingPath(id), removeError);
            auto manifest=RecordingPath(id); manifest.replace_extension(".json");
            std::filesystem::remove(manifest,removeError);
        } // Never recurse or remove unrelated files.
    }
}

int MyHighScores::TimeContext(int game, int index) const
{
    const auto& tables = m_times[Clamp(game)];
    return index >= 0 && index < static_cast<int>(tables.size()) ? tables[index].context : -1;
}

const MyHighScores::Entry& MyHighScores::TopTime(int game, int context, int rank) const
{
    if (rank < 0 || rank >= kTopCount) return m_empty;
    for (const auto& table : m_times[Clamp(game)]) if (table.context == context) return table.entries[rank];
    return m_empty;
}

bool MyHighScores::QualifiesTime(int game, int context, double seconds) const
{
    if (!(seconds > 0.0) || !std::isfinite(seconds) || context < 0) return false;
    for (const auto& table : m_times[Clamp(game)]) if (table.context == context)
        return table.entries[kTopCount - 1].raceSeconds <= 0 || seconds < table.entries[kTopCount - 1].raceSeconds;
    return m_times[Clamp(game)].size() < kMaxTimeContexts;
}

bool MyHighScores::RunQualifiesTime() const
{
    for (const RunTime& time : m_runTimes) if (QualifiesTime(m_active, time.context, time.seconds)) return true;
    return false;
}

void MyHighScores::InsertTime(int game, int context, const Entry& entry)
{
    if (!QualifiesTime(game, context, entry.raceSeconds)) return;
    auto& tables = m_times[Clamp(game)];
    auto found = std::find_if(tables.begin(), tables.end(), [context](const TimeTable& t) { return t.context == context; });
    if (found == tables.end()) { tables.push_back(TimeTable()); found = tables.end() - 1; found->context = context; }
    Entry* table = found->entries;
    int rank = 0;
    while (rank < kTopCount && table[rank].raceSeconds > 0 && table[rank].raceSeconds <= entry.raceSeconds) ++rank;
    if (rank == kTopCount) return;
    const uint64_t displaced = table[kTopCount - 1].replay;
    for (int i = kTopCount - 1; i > rank; --i) table[i] = table[i - 1];
    table[rank] = entry; table[rank].raceContext = context; m_dirty = true;
    DiscardRecording(displaced);
}

void MyHighScores::CommitTimes(const char* name, bool autonomous, double seconds, uint64_t replay)
{
    for (const RunTime& time : m_runTimes) {
        Entry entry; entry.score = m_lastScore; entry.wave = m_runWave;
        entry.seconds = seconds; entry.replay = replay; entry.raceSeconds = time.seconds;
        entry.demo = autonomous; entry.raceContext = time.context;
        int n = 0; for (; name && name[n] && n < kNameLen; ++n) entry.name[n] = name[n] == ' ' ? '_' : name[n];
        entry.name[n] = 0;
        InsertTime(m_active, time.context, entry);
    }
}

bool MyHighScores::RecordDemoScore(int game, int score, int wave, double seconds, uint64_t replay)
{
    if (m_muted) return false;
    if (game < 0 || game >= kMaxGames) return false;
    if (game == m_active) { m_lastScore = score; m_runWave = wave; CommitTimes("DEMO_MODE!", true, seconds, replay); }
    if (score <= 0) return false;
    Entry& best = m_demoBest[game];
    wave = (std::max)(1, wave);
    // Autonomous and human results use the same sorted insertion. The source
    // flag is presentation metadata, never a reason to remove a previous rank.
    const int rank = Insert(game, score, wave, "DEMO_MODE!", true, seconds, replay);
    if (score > best.score || (score == best.score && wave > best.wave)) {
        best = Entry(); best.score = score; best.wave = wave; best.demo = true;
        best.seconds = seconds; best.replay = rank >= 0 ? replay : 0;
        const char* name = "DEMO_MODE!"; for (int i = 0; i <= kNameLen; ++i) best.name[i] = name[i];
    }
    m_bestWave[game] = (std::max)(m_bestWave[game], wave);
    if (rank >= 0) m_dirty = true;
    return rank >= 0;
}

void MyHighScores::Load()
{
    std::ifstream in(m_file);
    std::string line;
    // Scores from the old rules cannot be compared with recorded runs under
    // the new rules. Migrate once; the schema marker prevents future resets.
    if (!std::getline(in, line) || line != "CPGE_HIGHSCORES_2") {
        m_dirty = true; Flush(); return;
    }
    while (std::getline(in, line))                         // a missing or damaged file just yields an empty table
    {
        std::istringstream ls(line);
        char tag = 0; int game = -1, a = 0, b = 1;
        std::string name;
        if (!(ls >> tag >> game >> a)) continue;
        if (game < 0 || game >= kMaxGames) continue;
        if (tag == 'S') { double seconds = 0, race = 0; uint64_t replay = 0; int context = -1; int demo = 0; ls >> b >> name >> seconds >> replay >> race >> context; ls >> demo; Insert(game, a, b, name.c_str(), demo != 0, seconds, replay, race, context); }
        else if (tag == 'D') { double seconds = 0; uint64_t replay = 0; ls >> b >> seconds >> replay; RecordDemoScore(game, a, b, seconds, replay); }
        else if (tag == 'W') m_bestWave[game] = a < 1 ? 1 : a;
        else if (tag == 'T') { Entry entry; int demo = 0; ls >> entry.raceSeconds >> entry.score >> entry.wave >> demo >> name >> entry.seconds >> entry.replay; entry.demo = demo != 0; int n = 0; for (; n < kNameLen && n < static_cast<int>(name.size()); ++n) entry.name[n] = name[n]; entry.name[n] = 0; InsertTime(game, a, entry); }
    }
    m_dirty = false;
}

void MyHighScores::Flush()
{
    if (!m_dirty || m_file.empty()) return;
    std::ofstream out(m_file, std::ios::trunc);
    if (!out) return;                                      // read-only location: scores just stay in memory
    m_dirty = false;
    out << "CPGE_HIGHSCORES_2\n"; out.precision(12);
    for (int g = 0; g < kMaxGames; ++g)
    {
        for (int r = 0; r < kTopCount; ++r)
            if (m_top[g][r].score > 0) out << "S " << g << ' ' << m_top[g][r].score << ' ' << m_top[g][r].wave << ' ' << m_top[g][r].name << ' ' << m_top[g][r].seconds << ' ' << m_top[g][r].replay << ' ' << m_top[g][r].raceSeconds << ' ' << m_top[g][r].raceContext << ' ' << m_top[g][r].demo << '\n';

        if (m_bestWave[g] > 1) out << "W " << g << ' ' << m_bestWave[g] << '\n';
        for (const auto& table : m_times[g]) for (const Entry& entry : table.entries) if (entry.raceSeconds > 0)
            out << "T " << g << ' ' << table.context << ' ' << entry.raceSeconds << ' ' << entry.score << ' ' << entry.wave << ' ' << entry.demo << ' ' << entry.name << ' ' << entry.seconds << ' ' << entry.replay << '\n';
    }
}

void MyHighScores::SubmitName(const char* name)
{
    if (!m_pending) return;
    m_pending = false; m_committed = true;
    m_lastRank = Insert(m_active, m_pendingScore, m_pendingWave, name, false, m_pendingSeconds, m_pendingReplay, m_pendingRaceSeconds, m_pendingRaceContext);
    CommitTimes(name && name[0] ? name : "PLAYER", false, m_pendingSeconds, m_pendingReplay);
    if (m_lastRank >= 0)
    {
        m_dirty = true;
        const Entry& e = m_top[Clamp(m_active)][m_lastRank];
        for (int i = 0; i <= kNameLen; ++i) m_lastName[i] = e.name[i];     // remembered for abandoned runs
    }
    Flush();
}

void MyHighScores::CommitRun()
{
    if (m_pending) SubmitName(m_lastName);                 // leaving while a name was being typed: use the last name
    if (m_committed || m_active < 0 || m_autonomous) return;
    m_committed = true;
    m_lastRank = Insert(m_active, m_lastScore, m_runWave, m_lastName, false, m_runSeconds, m_runReplay, m_raceSeconds, m_raceContext);
    CommitTimes(m_lastName, false, m_runSeconds, m_runReplay);
    if (m_lastRank >= 0) m_dirty = true;
}

void MyHighScores::SetActiveGame(int game, bool autonomous)
{
    CommitRun();                                           // an abandoned run still counts
    Flush();                                               // switching game is a safe moment for file IO
    m_active = game; m_autonomous = autonomous; m_runTimes.clear();
    m_runSeconds = m_raceSeconds = 0.0; m_raceContext = -1; m_runReplay = 0;
    m_runWave = 1; m_lastScore = 0; m_lastRank = -1; m_wasOver = false; m_committed = false; m_pending = false;
}

void MyHighScores::ReportWave(int wave)
{
    if (m_muted) return;
    if (wave > m_runWave) m_runWave = wave;
}

void MyHighScores::Update(int score, bool gameOver, float dt)
{
    if (m_muted) return;                                    // a state replay is showing an old run
    if (m_active < 0 || m_active >= kMaxGames) return;
    if (!m_pending && !gameOver && !m_wasOver && std::isfinite(dt) && dt > 0.0f) m_runSeconds += dt;
    if (!m_pending)
    {
        if (score < m_lastScore)                           // the score dropped back: the player restarted
        {
            CommitRun();
            m_runTimes.clear(); m_runReplay = 0;
            m_runWave = 1; m_runSeconds = m_raceSeconds = 0.0; m_raceContext = -1; m_committed = false; m_lastRank = -1;
        }
        else if (score > m_lastScore) m_committed = false; // a run in progress
        m_lastScore = score;
    }
    if (m_runWave > m_bestWave[m_active]) { m_bestWave[m_active] = m_runWave; m_dirty = true; }

    if (gameOver && !m_wasOver && !m_committed)            // the run just ended
    {
        if (Qualifies(m_lastScore) || RunQualifiesTime()) { m_pending = true; m_pendingScore = m_lastScore; m_pendingWave = m_runWave; m_pendingSeconds = m_runSeconds; m_pendingReplay = m_runReplay; m_pendingRaceSeconds = m_raceSeconds; m_pendingRaceContext = m_raceContext; m_lastRank = -1; }
        else { m_committed = true; m_lastRank = -1; }
        Flush();
    }
    m_wasOver = gameOver;

    m_saveClock += dt;
    if (m_saveClock > 5.0f) { m_saveClock = 0.0f; Flush(); }    // periodic autosave (no-op unless something changed)
}

// ================================================================================================
//  MyExplosionFX
// ================================================================================================
MyExplosionFX::MyExplosionFX(size_t capacity, uint32_t seed) : m_pool(capacity ? capacity : 1), m_rng(seed ? seed : 1u) {}

float MyExplosionFX::Rand01()
{
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0xFFFFFFu) / 16777216.0f;
}

MyVector MyExplosionFX::RandomUnit()
{
    // One table load instead of up to eight rejection-sampling rounds, each with three random numbers and a square root.
    m_rng = m_rng * 1664525u + 1013904223u;
    return MyVector(FAST_UNIT_SPHERE(m_rng));
}

MyExpParticle& MyExplosionFX::Slot()
{
    MyExpParticle& p = m_pool[m_next];                                  // recycle the oldest slot when the pool is full
    m_next = (m_next + 1) % m_pool.size();
    p = MyExpParticle();
    return p;
}

void MyExplosionFX::Shockwave(const MyVector& origin, const MyVector& normal, float startRadius, float endRadius, float life, float thickness,
                              uint8_t r, uint8_t g, uint8_t b, int ripples)
{
    MyExpParticle& p = Slot();
    p.type = MyExpType::Shockwave; p.pos = origin; p.normal = normal.Normalized();
    p.r0 = startRadius; p.r1 = endRadius; p.life = p.maxLife = life; p.thickness = thickness; p.ripples = (ripples > 1) ? 2 : 1;
    p.r = r; p.g = g; p.b = b;
}

void MyExplosionFX::Implosion(const MyVector& origin, const MyVector& normal, float startRadius, float life, uint8_t r, uint8_t g, uint8_t b)
{
    MyExpParticle& p = Slot();
    p.type = MyExpType::Implosion; p.pos = origin; p.normal = normal.Normalized();
    p.r0 = startRadius; p.r1 = 0.05f; p.life = p.maxLife = life; p.thickness = startRadius * 0.08f; p.ripples = 2;
    p.r = r; p.g = g; p.b = b;
}

void MyExplosionFX::Fireball(const MyVector& origin, float radius, float life, uint8_t r, uint8_t g, uint8_t b)
{
    MyExpParticle& p = Slot();
    p.type = MyExpType::Fireball; p.pos = origin; p.r0 = radius * 0.15f; p.r1 = radius; p.life = p.maxLife = life;
    p.r = r; p.g = g; p.b = b;
}

void MyExplosionFX::Sparks(const MyVector& origin, int count, float speed, float length, float life, uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity)
{
    count = MyMath::ScaleCount(count, m_density, Rand01());             // quality: fewer particles per call (stochastic rounding)
    for (int i = 0; i < count; ++i)
    {
        MyExpParticle& p = Slot();
        p.type = MyExpType::Sparks; p.pos = origin; p.vel = RandomUnit() * (speed * Rand(0.35f, 1.0f)) + inheritVelocity * 0.5f;
        p.life = p.maxLife = life * Rand(0.6f, 1.0f); p.size = length * Rand(0.6f, 1.2f); p.drag = 1.6f;
        const float tint = Rand(0.75f, 1.0f);
        p.r = static_cast<uint8_t>(r * tint); p.g = static_cast<uint8_t>(g * tint); p.b = static_cast<uint8_t>(b * tint);
    }
}

void MyExplosionFX::Smoke(const MyVector& origin, int count, float speed, float size, float life, uint8_t shade)
{
    count = MyMath::ScaleCount(count, m_density, Rand01());             // quality: fewer particles per call (stochastic rounding)
    for (int i = 0; i < count; ++i)
    {
        MyExpParticle& p = Slot();
        p.type = MyExpType::Smoke; p.pos = origin + RandomUnit() * (size * 0.3f);
        p.vel = RandomUnit() * (speed * Rand(0.2f, 0.5f)) + MyVector(0.0f, speed * 0.25f, 0.0f);
        p.r0 = size * 0.4f; p.r1 = size * Rand(1.2f, 1.9f); p.life = p.maxLife = life * Rand(0.7f, 1.0f); p.drag = 1.1f;
        const float sv = MyMath::Min(255.0f, static_cast<float>(shade) * Rand(0.8f, 1.1f));
        p.r = p.g = p.b = static_cast<uint8_t>(sv);
    }
}

void MyExplosionFX::Flak(const MyVector& origin, float size, float life)
{
    MyExpParticle& p = Slot();
    p.type = MyExpType::Flak; p.pos = origin; p.r0 = size * 0.3f; p.r1 = size; p.life = p.maxLife = life;
    p.r = 255; p.g = 80; p.b = 40;                                      // the red core; the puff itself is drawn near black
}

void MyExplosionFX::Blast(const MyVector& origin, const MyVector& normal, float size, uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity)
{
    Fireball(origin, size * 0.9f, 0.45f, r, g, b);
    Shockwave(origin, normal, size * 0.2f, size * 2.4f, 0.7f, size * 0.10f, r, g, b, 2);
    Sparks(origin, 10, size * 4.5f, size * 0.55f, 0.6f, 255, 230, 160, inheritVelocity);
    Smoke(origin, 2, size * 2.0f, size * 0.9f, 1.0f);
}

void MyExplosionFX::Nova(const MyVector& origin, const MyVector& normal, float size, uint8_t r, uint8_t g, uint8_t b, const MyVector& inheritVelocity)
{
    Fireball(origin, size, 0.65f, r, g, b);
    Fireball(origin, size * 0.55f, 0.35f, 255, 255, 230);               // the white-hot core
    Shockwave(origin, normal, size * 0.25f, size * 3.6f, 0.95f, size * 0.14f, r, g, b, 2);
    Shockwave(origin, normal, size * 0.15f, size * 2.2f, 0.60f, size * 0.09f, 255, 255, 235, 1);
    Sparks(origin, 26, size * 5.0f, size * 0.5f, 0.8f, 255, 235, 170, inheritVelocity);
    Smoke(origin, 5, size * 2.2f, size * 1.0f, 1.4f);
}

void MyExplosionFX::Update(float dt, const MyVector& gravity)
{
    m_alive = 0;
    for (MyExpParticle& p : m_pool)
    {
        if (p.life <= 0.0f) continue;
        p.life -= dt;
        if (p.life <= 0.0f) { p.life = 0.0f; continue; }
        ++m_alive;
        p.pos += p.vel * dt;
        if (p.type == MyExpType::Smoke || p.type == MyExpType::Sparks) p.vel += gravity * dt;
        if (p.drag > 0.0f) p.vel = p.vel * MyMath::Max(0.0f, 1.0f - p.drag * dt);
    }
}

void MyExplosionFX::Clear()
{
    for (MyExpParticle& p : m_pool) p.life = 0.0f;
    m_alive = 0;
}

float MyExplosionFX::Radius(const MyExpParticle& p)
{
    // Power curves are tables registered once (a thread-safe function-local static), so no std::pow runs per particle per frame.
    static const MathPrecalculation::PowerCurveId kShockOut = FAST_MATH.RegisterPowerCurve(2.2f);
    const float t = Progress(p);
    float e;
    switch (p.type)
    {
    case MyExpType::Implosion: e = t * t; break;                                   // slow at first, then it snaps shut
    case MyExpType::Fireball:  e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); break;
    case MyExpType::Smoke:     e = t; break;
    default:                   e = 1.0f - FAST_MATH.FastPowCurve(kShockOut, 1.0f - t); break;   // shock-waves: fast out of the blast, then they slow as they stretch
    }
    return p.r0 + (p.r1 - p.r0) * e;
}

float MyExplosionFX::Alpha(const MyExpParticle& p)
{
    static const MathPrecalculation::PowerCurveId kFade15 = FAST_MATH.RegisterPowerCurve(1.5f);
    static const MathPrecalculation::PowerCurveId kFade12 = FAST_MATH.RegisterPowerCurve(1.2f);
    static const MathPrecalculation::PowerCurveId kFade13 = FAST_MATH.RegisterPowerCurve(1.3f);
    const float t = Progress(p);
    switch (p.type)
    {
    case MyExpType::Shockwave: return FAST_MATH.FastPowCurve(kFade15, 1.0f - t);                    // fades as it stretches further out
    case MyExpType::Implosion: return FAST_MATH.FastPowCurve(kFade12, t);                           // brightens as it closes in
    case MyExpType::Fireball:  return (t < 0.2f) ? 1.0f : FAST_MATH.FastPowCurve(kFade13, 1.0f - (t - 0.2f) * 1.25f);
    case MyExpType::Smoke:     return 0.65f * (1.0f - t);
    default:                   return 1.0f - t;                                    // sparks, flak
    }
}

// ================================================================================================
//  MyBlastStyles - see Vectors.h.  Everything is spawned into a fixed pool: no allocation while playing.
// ================================================================================================
MyBlastStyles::MyBlastStyles(size_t capacity, uint32_t seed) : m_pool(capacity ? capacity : 1), m_rng(seed ? seed : 1u) {}

float MyBlastStyles::Rand01()
{
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>((m_rng >> 8) & 0xFFFFFFu) / 16777216.0f;
}

MyVector MyBlastStyles::RandomUnit()
{
    m_rng = m_rng * 1664525u + 1013904223u;
    return MyVector(FAST_UNIT_SPHERE(m_rng));                                   // table lookup instead of rejection sampling
}

MyBlastParticle& MyBlastStyles::Slot()
{
    MyBlastParticle& p = m_pool[m_next];                                        // ring buffer: the oldest slot is recycled when the pool is full
    m_next = (m_next + 1) % m_pool.size();
    p = MyBlastParticle();
    p.orient = MyQuaternion::Identity();
    return p;
}

MyBlastParticle& MyBlastStyles::Ember(const MyVector& pos, const MyVector& vel, float size, float life, float drag, float grav, uint8_t r, uint8_t g, uint8_t b)
{
    MyBlastParticle& p = Slot();
    p.type = MyBlastType::Ember; p.pos = pos; p.vel = vel; p.size = size; p.life = p.maxLife = life; p.drag = drag; p.grav = grav;
    p.r = r; p.g = g; p.b = b;
    return p;
}

MyBlastParticle& MyBlastStyles::Shard(const MyVector& pos, const MyVector& vel, float size, float flat, float life, float spin, uint8_t r, uint8_t g, uint8_t b)
{
    MyBlastParticle& p = Slot();
    p.type = MyBlastType::Shard; p.pos = pos; p.vel = vel; p.size = size; p.flat = flat; p.life = p.maxLife = life; p.drag = 0.6f;
    p.spin = spin; p.spinAxis = RandomUnit(); p.orient = MyQuaternion::AngleAxis(Rand(0.0f, 360.0f), RandomUnit());
    p.r = r; p.g = g; p.b = b;
    return p;
}

MyBlastParticle& MyBlastStyles::Ring(const MyVector& pos, const MyVector& normal, float r0, float r1, float life, float thick, float delay, uint8_t r, uint8_t g, uint8_t b)
{
    MyBlastParticle& p = Slot();
    p.type = MyBlastType::Ring; p.pos = pos; p.dir = normal; p.r0 = r0; p.r1 = r1; p.life = p.maxLife = life; p.thick = thick; p.delay = delay;
    p.r = r; p.g = g; p.b = b;
    return p;
}

MyBlastParticle& MyBlastStyles::Orb(const MyVector& pos, float r0, float r1, float life, float delay, uint8_t r, uint8_t g, uint8_t b)
{
    MyBlastParticle& p = Slot();
    p.type = MyBlastType::Orb; p.pos = pos; p.r0 = r0; p.r1 = r1; p.life = p.maxLife = life; p.delay = delay;
    p.r = r; p.g = g; p.b = b;
    return p;
}

MyBlastStyle MyBlastStyles::Pick(uint32_t allowedMask)
{
    allowedMask &= (1u << static_cast<int>(MyBlastStyle::Count)) - 1u;
    if (!allowedMask) allowedMask = 1u;
    int n = 0;
    for (uint32_t m = allowedMask; m; m &= m - 1) ++n;
    if (n > 1 && m_last >= 0) allowedMask &= ~(1u << m_last), --n;               // no immediate repeat when there is a choice
    int k = static_cast<int>(Rand01() * static_cast<float>(n));
    if (k >= n) k = n - 1;
    for (int i = 0; i < static_cast<int>(MyBlastStyle::Count); ++i)
        if ((allowedMask >> i) & 1u) { if (k-- == 0) return static_cast<MyBlastStyle>(i); }
    return MyBlastStyle::Firework;
}

void MyBlastStyles::Spawn(MyBlastStyle style, const MyVector& origin, const MyVector& normal, float s, uint8_t r, uint8_t g, uint8_t b, const MyVector& inherit)
{
    m_last = static_cast<int>(style);
    const MyVector n = normal.Normalized();
    MyVector u = n.Cross(MyVector::Up());                                       // an in-plane basis (u, v) for rings / fans
    if (u.SqrMagnitude() < 1.0e-4f) u = n.Cross(MyVector::Right());
    u = u.Normalized();
    const MyVector v = n.Cross(u);
    const MyVector drift = inherit * 0.4f;                                      // debris keeps a little of the target's momentum
    // lerp a colour towards white (f = 0 keeps it, 1 is white-hot)
    auto hot = [](uint8_t c, float f) { return static_cast<uint8_t>(c + (255 - c) * f); };

    // a streak (Bolt) along `d`: centred on `pos`, flying at `speed`, slowed by `drag` (Slot() is private, so this lives here)
    auto bolt = [&](const MyVector& pos, const MyVector& d, float len, float speed, float thick, float life, float delay, float drag, uint8_t R, uint8_t G, uint8_t B)
    {
        MyBlastParticle& p = Slot();
        p.type = MyBlastType::Bolt; p.pos = pos; p.dir = d; p.vel = d * speed; p.size = len; p.thick = thick; p.life = p.maxLife = life; p.delay = delay; p.drag = drag;
        p.r = R; p.g = G; p.b = B;
    };

    switch (style)
    {
    case MyBlastStyle::Firework:
    {
        const int cnt = Count(24);
        for (int i = 0; i < cnt; ++i)
        {
            const bool gold = (i & 3) == 0;                                     // every fourth ember is gold for contrast
            Ember(origin, RandomUnit() * (s * Rand(2.2f, 4.0f)) + drift, s * Rand(0.09f, 0.14f), 1.2f * Rand(0.75f, 1.0f), 1.3f, 0.8f,
                  gold ? 255 : hot(r, 0.25f), gold ? 210 : hot(g, 0.25f), gold ? 90 : hot(b, 0.25f));
        }
        const int pops = Count(10);                                             // second stage: embers that only appear (mid-flight) after a delay = crackle
        for (int i = 0; i < pops; ++i)
            Ember(origin, RandomUnit() * (s * Rand(1.5f, 2.5f)) + drift, s * 0.07f, 0.45f, 1.0f, 0.6f, 255, 240, 200).delay = Rand(0.35f, 0.6f);
        Orb(origin, s * 0.2f, s * 0.9f, 0.2f, 0.0f, 255, 255, 230);
        break;
    }
    case MyBlastStyle::Starburst:
    {
        Orb(origin, s * 0.3f, s * 1.1f, 0.22f, 0.0f, 255, 255, 240);
        Ring(origin, n, s * 0.2f, s * 2.6f, 0.5f, s * 0.05f, 0.0f, r, g, b);
        int rays = Count(12);
        if (rays < 6) rays = 6;
        rays &= ~1;                      // an even count, so long and short rays alternate evenly
        for (int i = 0; i < rays; ++i)
        {
            const float a = static_cast<float>(i) * MyMath::kTwoPi / static_cast<float>(rays) + Rand(-0.05f, 0.05f);
            const MyVector d = u * std::cos(a) + v * std::sin(a);
            const bool lng = (i & 1) == 0;
            MyBlastParticle& p = Slot();
            p.type = MyBlastType::Bolt; p.pos = origin; p.dir = d; p.vel = d * (s * (lng ? 5.0f : 3.0f)); p.size = s * (lng ? 2.6f : 1.4f);
            p.thick = s * 0.06f; p.life = p.maxLife = 0.45f; p.drag = 3.0f;
            p.r = hot(r, lng ? 0.7f : 0.3f); p.g = hot(g, lng ? 0.7f : 0.3f); p.b = hot(b, lng ? 0.7f : 0.3f);
        }
        break;
    }
    case MyBlastStyle::Vortex:
    {
        Orb(origin, s * 0.15f, s * 0.7f, 0.3f, 0.0f, 255, 255, 230);
        const int per = Count(14);
        for (int arm = 0; arm < 2; ++arm)
            for (int i = 0; i < per; ++i)
            {
                const float f = static_cast<float>(i) / static_cast<float>(per);         // 0 at the core .. 1 at the arm tip
                const float a = static_cast<float>(arm) * MyMath::kPi + static_cast<float>(i) * 0.12f;
                const MyVector d = u * std::cos(a) + v * std::sin(a);
                MyBlastParticle& e = Ember(origin, d * (s * (1.4f + 2.6f * f)) + drift, s * 0.1f, Rand(0.8f, 1.0f), 0.5f, 0.0f, hot(r, f * 0.6f), hot(g, f * 0.6f), hot(b, f * 0.6f));
                e.spin = 4.5f; e.spinAxis = n;                                  // the velocity curls round the normal: a spiral
            }
        break;
    }
    case MyBlastStyle::Shatter:
    {
        const int cnt = Count(16);
        for (int i = 0; i < cnt; ++i)
        {
            const float tint = Rand(0.75f, 1.0f);
            Shard(origin, RandomUnit() * (s * Rand(1.5f, 4.0f)) + inherit * 0.6f, s * Rand(0.2f, 0.45f), 0.14f, 1.2f * Rand(0.7f, 1.0f),
                  Rand(5.0f, 12.0f) * ((i & 1) ? 1.0f : -1.0f), static_cast<uint8_t>(r * tint), static_cast<uint8_t>(g * tint), static_cast<uint8_t>(b * tint));
        }
        const int sp = Count(8);
        for (int i = 0; i < sp; ++i) Ember(origin, RandomUnit() * (s * 4.0f) + drift, s * 0.05f, 0.35f, 1.5f, 0.3f, 255, 240, 200);
        Ring(origin, n, s * 0.2f, s * 1.8f, 0.35f, s * 0.03f, 0.0f, 255, 255, 255);
        break;
    }
    case MyBlastStyle::Bloom:
    {
        for (int k = 0; k < 3; ++k)                                             // three shells, each a little later, bigger and whiter than the last
            Orb(origin, s * 0.2f, s * (1.4f + 0.8f * static_cast<float>(k)), 0.55f + 0.1f * static_cast<float>(k), 0.12f * static_cast<float>(k),
                hot(r, 0.35f * static_cast<float>(k)), hot(g, 0.35f * static_cast<float>(k)), hot(b, 0.35f * static_cast<float>(k)));
        Ring(origin, n, s * 0.2f, s * 3.0f, 0.6f, s * 0.04f, 0.05f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        const int spores = Count(12);
        for (int i = 0; i < spores; ++i)                                        // slow glowing spores that drift upward
            Ember(origin + RandomUnit() * (s * 0.3f), RandomUnit() * (s * Rand(0.6f, 1.6f)) + drift, s * 0.07f, 1.3f * Rand(0.7f, 1.0f), 0.6f, -0.1f, hot(r, 0.4f), hot(g, 0.4f), hot(b, 0.4f));
        break;
    }
    case MyBlastStyle::Collapse:
    {
        const float T = 0.45f, total = 1.2f;                                    // T seconds sucked in, the rest blown out
        const float R = s * 2.2f;
        const int cnt = Count(24);
        for (int i = 0; i < cnt; ++i)
        {
            const MyVector d = RandomUnit();
            MyBlastParticle& e = Ember(origin + d * R, d * (-R / T), s * 0.09f, total, 0.0f, 0.0f, hot(r, 0.3f), hot(g, 0.3f), hot(b, 0.3f));
            e.dir = d; e.flip = total - T; e.r1 = s * Rand(3.0f, 5.5f);         // when life falls to `flip` the ember reverses at speed r1
        }
        Ring(origin, n, R, s * 0.12f, T, s * 0.07f, 0.0f, r, g, b);             // the shrinking ring
        Orb(origin, s * 0.1f, s * 0.5f, T, 0.0f, 90, 30, 140);                  // a dark core
        Orb(origin, s * 0.3f, s * 1.6f, 0.3f, T, 255, 255, 240);                // the detonation flash
        Ring(origin, n, s * 0.2f, s * 3.0f, 0.55f, s * 0.08f, T, 255, 255, 255);
        break;
    }
    case MyBlastStyle::Confetti:
    {
        static const uint8_t kPal[6][3] = { {255, 70, 90}, {255, 200, 40}, {70, 220, 120}, {70, 170, 255}, {200, 100, 255}, {255, 255, 255} };
        const int cnt = Count(30);
        for (int i = 0; i < cnt; ++i)
        {
            MyVector d = RandomUnit();
            d.y = std::fabs(d.y) * 0.8f + 0.3f;                                 // a pop that favours upward
            const uint8_t* c = kPal[i % 6];
            const float dim = confettiDim;                                      // 1 normally; low at night so the paper is hard to see
            MyBlastParticle& p = Shard(origin, d * (s * Rand(1.5f, 3.5f)) + drift, s * Rand(0.12f, 0.22f), 0.08f, 1.7f * Rand(0.7f, 1.0f),
                                       Rand(8.0f, 18.0f) * ((i & 1) ? 1.0f : -1.0f), static_cast<uint8_t>(c[0] * dim), static_cast<uint8_t>(c[1] * dim), static_cast<uint8_t>(c[2] * dim));
            p.drag = 2.2f; p.grav = 0.35f;                                      // heavy air drag + light gravity: it flutters down
        }
        if (confettiDim > 0.5f) Orb(origin, s * 0.2f, s * 0.8f, 0.18f, 0.0f, 255, 255, 255);        // (no bright pop flash in the dark)
        break;
    }
    case MyBlastStyle::Lightning:
    {
        int bolts = Count(7);
        if (bolts < 3) bolts = 3;
        for (int k = 0; k < bolts; ++k)
        {
            MyVector dir = RandomUnit(), pt = origin;
            const float life = Rand(0.22f, 0.38f);
            for (int seg = 0; seg < 4; ++seg)                                   // a jagged chain; each link appears slightly later so it crawls outward
            {
                const MyVector nd = (dir + RandomUnit() * 0.6f).Normalized();
                const float len = s * Rand(0.7f, 1.1f);
                MyBlastParticle& p = Slot();
                p.type = MyBlastType::Bolt; p.pos = pt + nd * (len * 0.5f); p.dir = nd; p.size = len; p.thick = s * 0.07f;
                p.life = p.maxLife = life; p.delay = static_cast<float>(seg) * 0.03f;
                p.r = hot(static_cast<uint8_t>((r + 150) / 2), 0.4f); p.g = hot(static_cast<uint8_t>((g + 200) / 2), 0.4f); p.b = 255;
                pt += nd * len; dir = nd;
            }
        }
        Orb(origin, s * 0.25f, s * 0.9f, 0.16f, 0.0f, 230, 240, 255);
        const int sp = Count(8);
        for (int i = 0; i < sp; ++i) Ember(origin, RandomUnit() * (s * 3.5f) + drift, s * 0.05f, 0.3f, 1.5f, 0.2f, 220, 235, 255);
        break;
    }
    case MyBlastStyle::Gyro:
    {
        const MyVector nrm[3] = { n, u, v };
        for (int k = 0; k < 3; ++k)                                             // three orthogonal rings that expand and tumble
        {
            MyBlastParticle& p = Ring(origin, nrm[k], s * 0.25f, s * 2.6f, 0.85f, s * 0.05f, static_cast<float>(k) * 0.07f,
                                      hot(r, 0.3f * static_cast<float>(k)), hot(g, 0.3f * static_cast<float>(k)), hot(b, 0.3f * static_cast<float>(k)));
            p.spin = (k == 1) ? 2.5f : -2.0f; p.spinAxis = (k == 0) ? u : n;
        }
        Orb(origin, s * 0.25f, s * 1.0f, 0.3f, 0.0f, 255, 255, 235);
        const int sp = Count(8);
        for (int i = 0; i < sp; ++i) Ember(origin, RandomUnit() * (s * 3.0f) + drift, s * 0.06f, 0.5f, 1.5f, 0.3f, hot(r, 0.6f), hot(g, 0.6f), hot(b, 0.6f));
        break;
    }
    case MyBlastStyle::Supernova:
    {
        Orb(origin, s * 0.3f, s * 1.9f, 0.35f, 0.0f, 255, 255, 245);
        Orb(origin, s * 0.2f, s * 3.4f, 0.75f, 0.05f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        Ring(origin, n, s * 0.3f, s * 5.2f, 0.7f, s * 0.07f, 0.0f, 255, 255, 255);
        Ring(origin, u, s * 0.3f, s * 4.2f, 0.85f, s * 0.05f, 0.08f, hot(r, 0.3f), hot(g, 0.3f), hot(b, 0.3f));
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float>(i) * MyMath::kTwoPi / 8.0f;
            bolt(origin, u * std::cos(a) + v * std::sin(a), s * ((i & 1) ? 2.0f : 3.2f), s * 7.0f, s * 0.08f, 0.5f, 0.0f, 3.0f, 255, 255, 255);
        }
        const int cnt = Count(34);
        for (int i = 0; i < cnt; ++i)
            Ember(origin, RandomUnit() * (s * Rand(3.0f, 7.0f)) + drift, s * Rand(0.08f, 0.14f), Rand(0.8f, 1.3f), 1.6f, 0.4f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        break;
    }
    case MyBlastStyle::Meteors:
    {
        Orb(origin, s * 0.2f, s * 0.8f, 0.2f, 0.0f, 255, 255, 230);
        const int cnt = Count(10);
        for (int i = 0; i < cnt; ++i)
        {
            MyVector d = RandomUnit(); d.y = -std::fabs(d.y) * 0.7f - 0.3f; d = d.Normalized();                  // a fan that falls away downward
            const float sp = s * Rand(4.0f, 7.0f), dl = Rand(0.0f, 0.25f), life = Rand(0.5f, 0.8f);
            bolt(origin + RandomUnit() * (s * 0.3f), d, s * Rand(1.4f, 2.4f), sp, s * 0.06f, life, dl, 0.8f, hot(r, 0.6f), hot(g, 0.6f), hot(b, 0.6f));
            for (int k = 0; k < 2; ++k) Ember(origin, d * (sp * Rand(0.4f, 0.7f)) + drift, s * 0.06f, 0.6f, 1.2f, 0.3f, 255, 190, 110).delay = dl + 0.04f * static_cast<float>(k + 1);     // a trail of embers
        }
        break;
    }
    case MyBlastStyle::Fountain:
    {
        Ring(origin, MyVector::Up(), s * 0.2f, s * 2.2f, 0.5f, s * 0.05f, 0.0f, hot(r, 0.4f), hot(g, 0.4f), hot(b, 0.4f));
        const int cnt = Count(34);
        for (int i = 0; i < cnt; ++i)
        {
            const MyVector d = (MyVector::Up() + RandomUnit() * 0.35f).Normalized();
            Ember(origin, d * (s * Rand(3.0f, 5.0f)) + drift, s * 0.09f, Rand(1.1f, 1.6f), 0.15f, 1.4f, hot(r, 0.3f), hot(g, 0.3f), hot(b, 0.3f)).delay = Rand(0.0f, 0.4f);     // streamed out over 0.4 s
        }
        Orb(origin, s * 0.2f, s * 0.9f, 0.2f, 0.0f, 255, 255, 235);
        break;
    }
    case MyBlastStyle::Mushroom:
    {
        const MyVector up = MyVector::Up();
        const int stem = Count(14);
        for (int i = 0; i < stem; ++i)
            Ember(origin + RandomUnit() * (s * 0.2f), up * (s * Rand(2.0f, 3.2f)) + RandomUnit() * (s * 0.3f), s * 0.1f, Rand(0.8f, 1.1f), 1.0f, -0.15f, hot(r, 0.2f), hot(g, 0.2f), hot(b, 0.2f));
        Orb(origin, s * 0.3f, s * 1.4f, 0.25f, 0.0f, 255, 255, 235);
        Orb(origin + up * (s * 1.6f), s * 0.2f, s * 1.7f, 0.9f, 0.18f, hot(r, 0.3f), hot(g, 0.3f), hot(b, 0.3f));
        Orb(origin + up * (s * 2.4f), s * 0.2f, s * 2.4f, 1.0f, 0.3f, hot(r, 0.15f), hot(g, 0.15f), hot(b, 0.15f));
        Ring(origin + up * (s * 2.0f), up, s * 0.4f, s * 3.0f, 0.9f, s * 0.07f, 0.2f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        Ring(origin, up, s * 0.3f, s * 3.5f, 0.5f, s * 0.06f, 0.0f, 255, 255, 255);
        break;
    }
    case MyBlastStyle::Snowflake:
    {
        const uint8_t ir = hot(150, 0.3f), ig = hot(210, 0.3f);
        for (int k = 0; k < 6; ++k)
        {
            const float a = static_cast<float>(k) * MyMath::kPi / 3.0f;
            const MyVector d = u * std::cos(a) + v * std::sin(a);
            bolt(origin, d, s * 2.6f, s * 5.0f, s * 0.06f, 0.65f, 0.0f, 3.5f, ir, ig, 255);                                       // the arm
            for (int lv = 0; lv < 2; ++lv)                                                                                       // two levels of side branches, 60 degrees either side
            {
                const float t = (lv == 0) ? 0.45f : 0.75f;
                for (int sd = -1; sd <= 1; sd += 2)
                {
                    const float ab = a + static_cast<float>(sd) * MyMath::kPi / 3.0f;
                    bolt(origin + d * (s * 2.6f * t * 0.55f), u * std::cos(ab) + v * std::sin(ab), s * (lv == 0 ? 1.4f : 0.9f), s * 2.4f, s * 0.05f, 0.6f, 0.08f + 0.06f * static_cast<float>(lv), 3.5f, ir, ig, 255);
                }
            }
        }
        const int sp = Count(12);
        for (int i = 0; i < sp; ++i) Ember(origin, RandomUnit() * (s * 0.8f) + drift * 0.3f, s * 0.06f, 1.5f, 0.5f, -0.05f, 200, 230, 255);
        Orb(origin, s * 0.2f, s * 0.9f, 0.25f, 0.0f, 235, 245, 255);
        break;
    }
    case MyBlastStyle::Galaxy:
    {
        Orb(origin, s * 0.3f, s * 1.1f, 0.6f, 0.0f, 255, 250, 220);
        const int per = Count(14);
        for (int arm = 0; arm < 3; ++arm)
            for (int i = 0; i < per; ++i)
            {
                const float f = static_cast<float>(i) / static_cast<float>(per), a = static_cast<float>(arm) * MyMath::kTwoPi / 3.0f + f * 1.6f;
                const MyVector d = u * std::cos(a) + v * std::sin(a);
                MyBlastParticle& e = Ember(origin, d * (s * (0.8f + 2.4f * f)) + drift * 0.3f, s * (0.1f - 0.04f * f), Rand(1.2f, 1.6f), 0.35f, 0.0f, hot(r, 0.2f + 0.5f * f), hot(g, 0.2f + 0.5f * f), hot(b, 0.4f + 0.5f * f));
                e.spin = 2.8f; e.spinAxis = n;
            }
        break;
    }
    case MyBlastStyle::Petals:
    {
        Orb(origin, s * 0.2f, s * 0.8f, 0.3f, 0.0f, 255, 245, 240);
        for (int p = 0; p < 8; ++p)
        {
            const float a = static_cast<float>(p) * MyMath::kTwoPi / 8.0f;
            const MyVector d = u * std::cos(a) + v * std::sin(a);
            for (int j = 0; j < 5; ++j)
            {
                MyBlastParticle& e = Ember(origin, d * (s * (1.2f + 0.55f * static_cast<float>(j))), s * 0.1f, 1.0f, 0.9f, 0.0f, hot(r, 0.1f * static_cast<float>(j)), hot(g, 0.1f * static_cast<float>(j)), hot(b, 0.1f * static_cast<float>(j)));
                e.spin = ((j & 1) ? 3.2f : -3.2f); e.spinAxis = n;                                                                // alternate sparks curl either way: the petal's two edges
            }
        }
        break;
    }
    case MyBlastStyle::Pulse:
    {
        for (int k = 0; k < 5; ++k)
            Ring(origin, n, s * 0.2f, s * (2.0f + 0.9f * static_cast<float>(k)), 0.55f + 0.08f * static_cast<float>(k), s * 0.04f, 0.09f * static_cast<float>(k),
                 hot(r, 0.15f * static_cast<float>(k)), hot(g, 0.15f * static_cast<float>(k)), hot(b, 0.15f * static_cast<float>(k)));
        Orb(origin, s * 0.2f, s * 1.0f, 0.3f, 0.0f, 255, 255, 240);
        const int sp = Count(8);
        for (int i = 0; i < sp; ++i) Ember(origin, RandomUnit() * (s * 2.5f) + drift, s * 0.06f, 0.5f, 1.4f, 0.3f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        break;
    }
    case MyBlastStyle::Fireflies:
    {
        Orb(origin, s * 0.15f, s * 0.7f, 0.2f, 0.0f, 255, 255, 200);
        const int cnt = Count(18);
        for (int i = 0; i < cnt; ++i)
        {
            MyBlastParticle& e = Ember(origin + RandomUnit() * (s * 1.2f), RandomUnit() * (s * 0.5f) + drift * 0.2f, s * 0.07f, Rand(1.4f, 2.0f), 0.3f, -0.03f, hot(r, 0.3f), hot(g, 0.5f), 120);
            e.spin = Rand(-2.0f, 2.0f); e.spinAxis = RandomUnit();                                                               // each one wanders on its own curl
        }
        break;
    }
    case MyBlastStyle::Flame:
    {
        Orb(origin, s * 0.2f, s * 1.0f, 0.25f, 0.0f, 255, 240, 190);
        const int cnt = Count(36);
        for (int i = 0; i < cnt; ++i)
        {
            const float f = Rand01();                                           // 0 white-hot .. 1 deep red
            const MyVector d = (n + RandomUnit() * 0.28f).Normalized();
            Ember(origin, d * (s * Rand(2.5f, 5.5f)) + drift, s * Rand(0.12f, 0.2f), Rand(0.5f, 0.9f), 2.2f, -0.4f,
                  255, static_cast<uint8_t>(240 - 170 * f), static_cast<uint8_t>(190 * (1.0f - f) * (1.0f - f)));
        }
        break;
    }
    case MyBlastStyle::NovaCross:
    {
        Orb(origin, s * 0.3f, s * 1.2f, 0.25f, 0.0f, 255, 255, 245);
        Ring(origin, n, s * 0.2f, s * 2.8f, 0.5f, s * 0.05f, 0.0f, hot(r, 0.5f), hot(g, 0.5f), hot(b, 0.5f));
        for (int i = 0; i < 8; ++i)
        {
            const float a = static_cast<float>(i) * MyMath::kPi / 4.0f;
            const bool major = (i & 1) == 0;                                    // up / down / left / right are long, the diagonals short
            bolt(origin, u * std::cos(a) + v * std::sin(a), s * (major ? 3.4f : 1.5f), s * (major ? 5.5f : 3.0f), s * (major ? 0.07f : 0.05f), 0.5f, 0.0f, 3.0f, hot(r, major ? 0.8f : 0.4f), hot(g, major ? 0.8f : 0.4f), hot(b, major ? 0.8f : 0.4f));
        }
        const int sp = Count(8);
        for (int i = 0; i < sp; ++i)
        {
            const float a = static_cast<float>(i & 3) * MyMath::kPi * 0.5f;
            Ember(origin, (u * std::cos(a) + v * std::sin(a)) * (s * Rand(1.0f, 3.5f)) + drift * 0.3f, s * 0.06f, 0.6f, 1.2f, 0.1f, 255, 255, 235);
        }
        break;
    }
    case MyBlastStyle::CubeCascade:
    {
        const int cnt = Count(18);
        for (int i = 0; i < cnt; ++i)
        {
            const float tint = Rand(0.75f, 1.0f);
            MyBlastParticle& p = Shard(origin, RandomUnit() * (s * Rand(1.5f, 3.2f)) + MyVector::Up() * (s * 1.2f) + inherit * 0.6f, s * Rand(0.18f, 0.34f), 1.0f, Rand(1.1f, 1.6f),
                                       Rand(4.0f, 10.0f) * ((i & 1) ? 1.0f : -1.0f), static_cast<uint8_t>(r * tint), static_cast<uint8_t>(g * tint), static_cast<uint8_t>(b * tint));
            p.drag = 0.2f; p.grav = 2.2f;                                       // heavy blocks: they tumble and FALL
        }
        Orb(origin, s * 0.2f, s * 1.0f, 0.18f, 0.0f, 255, 255, 235);
        Ring(origin, n, s * 0.2f, s * 2.0f, 0.35f, s * 0.04f, 0.0f, 255, 255, 255);
        break;
    }
    case MyBlastStyle::Helix:
    {
        Orb(origin, s * 0.2f, s * 0.9f, 0.25f, 0.0f, 255, 255, 235);
        const int per = Count(16);
        for (int k = 0; k < 2; ++k)
        {
            const float a0 = static_cast<float>(k) * MyMath::kPi;
            const MyVector perp = u * std::cos(a0) + v * std::sin(a0);
            for (int i = 0; i < per; ++i)                                       // each ember leaves a little after the one before it, so the strand winds along the axis
            {
                MyBlastParticle& e = Ember(origin, perp * (s * 2.2f) + n * (s * 1.2f) + drift * 0.3f, s * 0.09f, 0.75f, 0.4f, 0.0f, hot(r, 0.2f + 0.4f * static_cast<float>(k)), hot(g, 0.2f + 0.4f * static_cast<float>(k)), hot(b, 0.2f + 0.4f * static_cast<float>(k)));
                e.spin = 7.0f; e.spinAxis = n; e.delay = static_cast<float>(i) * 0.025f;
            }
        }
        break;
    }
    default:                                                                    // Pixelate: grid-snapped cubes on the world axes that blink out
    {
        static const MyVector kAxes[6] = { MyVector(1, 0, 0), MyVector(-1, 0, 0), MyVector(0, 1, 0), MyVector(0, -1, 0), MyVector(0, 0, 1), MyVector(0, 0, -1) };
        const int cnt = Count(22);
        for (int i = 0; i < cnt; ++i)
        {
            const int ax = MyMath::Min(5, static_cast<int>(Rand01() * 6.0f));
            const float sp = s * 0.9f * static_cast<float>(1 + MyMath::Min(2, static_cast<int>(Rand01() * 3.0f)));   // 3 discrete speeds
            MyBlastParticle& p = Shard(origin, kAxes[ax] * sp + drift * 0.5f, s * Rand(0.16f, 0.3f), 1.0f, Rand(0.7f, 1.1f), 0.0f, r, g, b);
            p.spin = 0.0f; p.orient = MyQuaternion::Identity(); p.drag = 0.0f; p.grav = 0.0f; p.thick = s * 0.25f;   // thick = the snap grid
        }
        Orb(origin, s * 0.2f, s * 0.8f, 0.14f, 0.0f, 255, 255, 255);
        break;
    }
    }
}

void MyBlastStyles::Update(float dt, const MyVector& gravity)
{
    m_alive = 0;
    for (MyBlastParticle& p : m_pool)
    {
        if (p.life <= 0.0f) continue;
        if (p.delay > 0.0f) p.delay -= dt;                                      // staged particles are simulated but do not age or draw yet
        else { p.life -= dt; if (p.life <= 0.0f) { p.life = 0.0f; continue; } }
        ++m_alive;
        switch (p.type)
        {
        case MyBlastType::Ember:
        case MyBlastType::Shard:
        {
            if (p.flip > 0.0f && p.life <= p.flip) { p.vel = p.dir * p.r1; p.flip = 0.0f; p.drag = 1.4f; p.grav = 0.5f; }   // Collapse: reverse into the blast
            if (p.type == MyBlastType::Ember && p.spin != 0.0f)                 // curl the velocity about the axis (exact for a velocity perpendicular to it)
            {
                const float c = std::cos(p.spin * dt), sn = std::sin(p.spin * dt);
                p.vel = p.vel * c + p.spinAxis.Cross(p.vel) * sn;
            }
            else if (p.type == MyBlastType::Shard && p.spin != 0.0f)
                p.orient = (MyQuaternion::AngleAxis(p.spin * dt * 57.2957795f, p.spinAxis) * p.orient).Normalized();
            p.pos += p.vel * dt;
            p.vel = (p.vel + gravity * (p.grav * dt)) * MyMath::Max(0.0f, 1.0f - p.drag * dt);
            break;
        }
        case MyBlastType::Bolt:                                                 // rays move outward; lightning links stay put (vel = 0)
            p.pos += p.vel * dt;
            if (p.drag > 0.0f) p.vel = p.vel * MyMath::Max(0.0f, 1.0f - p.drag * dt);
            break;
        case MyBlastType::Ring:
            if (p.spin != 0.0f)                                                 // tumble the ring's plane (Rodrigues rotation of the normal)
            {
                const float c = std::cos(p.spin * dt), sn = std::sin(p.spin * dt);
                p.dir = (p.dir * c + p.spinAxis.Cross(p.dir) * sn + p.spinAxis * (p.spinAxis.Dot(p.dir) * (1.0f - c))).Normalized();
            }
            break;
        default: break;
        }
    }
}

void MyBlastStyles::Clear()
{
    for (MyBlastParticle& p : m_pool) p.life = 0.0f;
    m_alive = 0;
}

float MyBlastStyles::Radius(const MyBlastParticle& p)
{
    const float t = Progress(p);
    const float e = 1.0f - (1.0f - t) * (1.0f - t);                             // ease-out: fast at first, then it settles
    return p.r0 + (p.r1 - p.r0) * e;
}

float MyBlastStyles::Alpha(const MyBlastParticle& p)
{
    const float t = Progress(p);
    switch (p.type)
    {
    case MyBlastType::Ember: return (t < 0.5f) ? 1.0f : 2.0f * (1.0f - t);
    case MyBlastType::Shard: return (t < 0.7f) ? 1.0f : (1.0f - t) / 0.3f;
    case MyBlastType::Ring:  return (1.0f - t) * (1.0f - t);
    case MyBlastType::Orb:   return 0.9f * (1.0f - t);
    default:                 return 1.0f - t;
    }
}
