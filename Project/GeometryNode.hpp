#pragma once

#include "SceneNode.hpp"
#include <vector>

//rectangular hitbox
class Hitbox{
public:
	glm::dvec4 _pos;
	glm::dvec3 _maxXYZ;
	//double _width;
	//double _height;
	//double _depth;

	Hitbox() : _pos(glm::dvec4(0, 0, 0, 1)), 
		_maxXYZ(glm::dvec3(1.0f)){ }
	double x(){ return _pos[0] - _maxXYZ.x/2.0; }
	double z(){ return _pos[2] + _maxXYZ.z/2.0; }
	double y(){ return _pos[1] - _maxXYZ.y/2.0; }

	double x1(){ return _pos[0] + _maxXYZ.x/2.0; }
	double z1(){ return _pos[2] - _maxXYZ.z/2.0; }
	double y1(){ return _pos[1] + _maxXYZ.y/2.0; }

};

class GeometryNode : public SceneNode {
public:
	GeometryNode(
		const std::string & meshId,
		const std::string & name,
		double hitwidth = 1.0,
		double hitheight = 1.0,
		double hitdepth = 1.0
	);

	void translate(const glm::vec3& amount);
	void scale(const glm::vec3& amount);

	bool collide3D(GeometryNode* other);
	bool collide2D(GeometryNode* other); //

	Material material;

	Hitbox *hitbox;
	std::vector<glm::vec3> m_vertices;
	std::vector<glm::vec3> m_faces;
	std::vector<glm::vec3> m_normals;
	// Mesh Identifier. This must correspond to an object name of
	// a loaded .obj file.
	std::string meshId;
};
