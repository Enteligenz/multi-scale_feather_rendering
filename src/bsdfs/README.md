# Additions to materials
_iridescentmicrofacet_ adds an iridescent material based on the paper ["A Practical Extension to Microfacet Theory for the Modeling of Varying Iridescence"](https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html)

_fakesss_ and _fakeiridescent_ are test materials that try to replicate some effects in a simplified way for testing.
_fakesss_ tries to fake subsurface scattering, while _fakeiridescent_ changes color based on the viewing angle.
Note that both of these do not exist in Blender and will thus have to be written into .xml files manually.
They are mostly unused at this point.

## Iridescent Microfacet
This material takes the following parameters in the .xml file:
* _reflectance_
* _roughness_ ∈ [0.01, 1.0], this is essentially alpha.
* _dinc_ ∈ [0.0, 10.0], iridescent effects vanish if this value gets too large.
* _eta2_ ∈ [1.0, 5.0]
* _eta3_ ∈ [1.0, 5.0]
* _kappa3_ ∈ [0.0, 5.0]
The easiest way to find good values for these parameters is by using Disney's BRDF explorer with the material given by the paper and trying out different parameters.

<!-- _fakesss_ only has one parameter, "translucency". It determines the probability that the ray does not change sides when hitting the material, which effectively is the probability that the material behaves like a normal diffuse one.

The colors of _fakeiridescent_ currently are hard-coded, ideally this will be changed in the future. It has the following parameters:
* _roughness_
* _metallic_
* _specular_
* _ior_