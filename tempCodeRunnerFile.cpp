void rk4Step(Ray &ray, double d_lambda, double rs) {
  double y0[6] = {ray.r, ray.theta, ray.phi, ray.dr, ray.dtheta, ray.dphi};
  double k1[6], k2[6], k3[6], k4[6], temp[6];

  if (ray.r < rs)
    return;
  // k1
  geodesicRHS(ray, k1, rs);

  // k2
  addState(y0, k1, d_lambda / 2.0, temp);
  Ray r2 = ray;
  r2.r = temp[0];
  r2.theta = temp[1];
  r2.phi = temp[2];
  r2.dr = temp[3];
  r2.dtheta = temp[4];
  r2.dphi = temp[5];

  geodesicRHS(r2, k2, rs);

  // k3
  addState(y0, k2, d_lambda / 2.0, temp);
  Ray r3 = ray;
  r3.r = temp[0];
  r3.theta = temp[1];
  r3.phi = temp[2];
  r3.dr = temp[3];
  r3.dtheta = temp[4];
  r3.dphi = temp[5];

  geodesicRHS(r3, k3, rs);

  // k4
  addState(y0, k3, d_lambda, temp);
  Ray r4 = ray;
  r4.r = temp[0];
  r4.theta = temp[1];
  r4.phi = temp[2];
  r4.dr = temp[3];
  r4.dtheta = temp[4];
  r4.dphi = temp[5];

  geodesicRHS(r4, k4, rs);

  // final step
  ray.r += (d_lambda / 6.0) * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]);
  ray.theta += (d_lambda / 6.0) * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]);
  ray.phi += (d_lambda / 6.0) * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]);
  ray.dr += (d_lambda / 6.0) * (k1[3] + 2 * k2[3] + 2 * k3[3] + k4[3]);
  ray.dtheta += (d_lambda / 6.0) * (k1[4] + 2 * k2[4] + 2 * k3[4] + k4[4]);
  ray.dphi += (d_lambda / 6.0) * (k1[5] + 2 * k2[5] + 2 * k3[5] + k4[5]);
}