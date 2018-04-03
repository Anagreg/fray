/***************************************************************************
 *   Copyright (C) 2009-2018 by Veselin Georgiev, Slavomir Kaslev et al    *
 *   admin@raytracing-bg.net                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
/**
 * @File main.cpp
 * @Brief Raytracer main file
 */
#include <math.h>
#include <vector>
#ifndef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL.h>
#endif // _WIN32
#include "util.h"
#include "sdl.h"
#include "color.h"
#include "vector.h"
#include "matrix.h"
#include "camera.h"
#include "geometry.h"
#include "shading.h"
#include "environment.h"
using namespace std;

Color vfb[VFB_MAX_SIZE][VFB_MAX_SIZE];

struct Node: public Intersectable {
	Geometry* geometry;
	Shader* shader;
	Transform T;
	
	bool intersect(Ray ray, IntersectionInfo& info)
	{
		Ray localRay = ray;
		localRay.start = T.untransformPoint(ray.start);
		localRay.dir = T.untransformDir(ray.dir);
		
		if (!geometry->intersect(localRay, info)) return false;
		
		info.ip = T.transformPoint(info.ip);
		info.norm = T.transformDir(info.norm);
		info.dist = distance(ray.start, info.ip);
		return true;
	}
};

Camera camera;
vector<Node> nodes;
Vector lightPos (100, 200, -80);
Color lightColor(1, 1, 0.9);
double lightIntensity = 50000;
Color ambientLightColor = Color(1, 1, 1) * 0.5;
bool antialiasing = false;
int sphereIndex;
int cubeIndex;
CubemapEnvironment env;

void setupScene()
{
	Node plane;
	
	env.loadMaps("data/env/forest");
	
	//CheckerTexture* checkerBW = new CheckerTexture();
	BitmapTexture* floorTiles = new BitmapTexture("data/floor.bmp");
	floorTiles->scaling = 1/100.0;
	BitmapTexture* world = new BitmapTexture("data/world.bmp");
	CheckerTexture* checkerColor = new CheckerTexture(Color(1, 0.5, 0.5), Color(0.5, 1.0, 1.0));
	CheckerTexture* checkerCube = new CheckerTexture(Color(0, 0, 1), Color(0.1, 0.1, 0.1));
	checkerCube->scaling = 0.2;

	plane.geometry = new Plane(80);
	
	Layered* planeShader = new Layered;
	
	planeShader->addLayer(new Lambert(floorTiles), Color(1, 1, 1));
	planeShader->addLayer(new Reflection(1), Color(1, 1, 1) * 0.01);
	
	plane.shader = planeShader;
	nodes.push_back(plane);
	
	Node sphere;
	sphere.geometry = new Sphere(Vector(0, 0, 0), 30);
	Phong* phong = new Phong(checkerColor);
	phong->exponent = 20;
	phong->specularMultiplier = 0.7;
	sphere.T.translate(Vector(-10, 60, 0));
	sphere.shader = new Refraction(1.33, Color(1, 1, 1) * 0.95);
	sphereIndex = int(nodes.size());
	nodes.push_back(sphere);
	
	Node cube;
	cube.geometry = new Cube(Vector(0, 0, 0), 15);
	cube.T.rotate(toRadians(30), 0, toRadians(60));
	cube.T.translate(Vector(+40, 16, 30));
	cube.shader = new Lambert(checkerColor);
	cubeIndex = int(nodes.size());
	nodes.push_back(cube);
	
	camera.pos = Vector(0, 60, -120);
	camera.yaw = toRadians(-10);
	camera.pitch = toRadians(-15);
}

bool visible(const Vector& a, const Vector& b)
{
	Ray ray;
	ray.dir = b - a;
	ray.start = a;
	double maxDist = distance(a, b);
	ray.dir.normalize();
	
	for (auto node: nodes) {
		IntersectionInfo info;
		if (node.intersect(ray, info) && info.dist < maxDist) {
			return false;
		}
	}
	
	return true;
}

Color raytrace(Ray ray)
{
	
	if (ray.depth > MAX_TRACE_DEPTH) return Color(0, 0, 0);
	
	Node closestNode;
	IntersectionInfo closestIntersection;
	closestIntersection.dist = 1e99;
	
	for (auto node: nodes) {
		IntersectionInfo info;
		if (node.intersect(ray, info) && info.dist < closestIntersection.dist) {
			closestIntersection = info;
			closestNode = node;
		}
	}
	
	if (closestIntersection.dist >= 1e99) {
		if (env.loaded) return env.getEnvironment(ray.dir);
		else return Color(0, 0, 0);
	}
		
	
	return closestNode.shader->shade(ray, closestIntersection);
}

Color raytrace(double x, double y)
{
	return raytrace(camera.getScreenRay(x, y));
}

void render()
{
	unsigned startTicks = SDL_GetTicks();
	camera.beginFrame();
	
	const double offsets[5][2] = {
		{ 0, 0 }, 
		{ 0.6, 0 },
		{ 0.3, 0.3 },
		{ 0, 0.6 },
		{ 0.6, 0.6 },
	};
	int numAASamples = COUNT_OF(offsets);
	
	for (int y = 0; y < frameHeight(); y++) {
		for (int x = 0; x < frameWidth(); x++) {
			Color avg = raytrace(x + offsets[0][0], y + offsets[0][1]);
			if (antialiasing) {
				for (int i = 1; i < numAASamples; i++)
					avg += raytrace(x + offsets[i][0], y + offsets[i][1]);
				vfb[y][x] = avg / numAASamples;
			} else {
				vfb[y][x] = avg; // divided by just one, so leave as is
			}
		}
	}
	unsigned elapsed = SDL_GetTicks() - startTicks;
	
	printf("Frame took %d ms\n", elapsed);
}

int main(int argc, char** argv)
{
	initGraphics(RESX, RESY);
	setupScene();
	Node& sphere = nodes[sphereIndex];
	Node& cube = nodes[cubeIndex];
	for (double offset = 0; offset < 1; offset += 10) {
		cube.T.translate(Vector(-10, 0, 0));
		//sphere.T.loadIdentity();
		//sphere.T.rotate(toRadians(angle), toRadians(-55), 0);
		//camera.yaw = toRadians(angle);
		camera.beginFrame();
		//double angleRad = toRadians(angle);
		//lightPos = Vector(cos(angleRad) * 200, 200, sin(angleRad) * 200);
		render();
		displayVFB(vfb);
	}
	waitForUserExit();
	closeGraphics();
	printf("Exited cleanly\n");
	return 0;
}
