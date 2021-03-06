#include "A3.hpp"
#include "scene_lua.hpp"
using namespace std;

#include "cs488-framework/GlErrorCheck.hpp"
#include "cs488-framework/MathUtils.hpp"
#include "GeometryNode.hpp"
#include "JointNode.hpp"
#include "trackball.hpp"

#include <imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stack>
#include <algorithm>

using namespace glm;

static bool show_gui = true;

const size_t CIRCLE_PTS = 48;


//----------------------------------------------------------------------------------------
// Constructor
A3::A3(const std::string & luaSceneFile)
	: m_luaSceneFile(luaSceneFile),
	  m_positionAttribLocation(0),
	  m_normalAttribLocation(0),
	  m_vao_meshData(0),
	  m_vbo_vertexPositions(0),
	  m_vbo_vertexNormals(0),
	  m_vao_arcCircle(0),
	  m_vbo_arcCircle(0),
	  m_mouseX(0.0),
	  m_mouseY(0.0),
	  tempMode(0),
	  m_mode(Mode::POSITION),
	  m_drawCircle(false),
	  m_zbuffer(true),
	  m_backfaceCulling(false),
	  m_frontfaceCulling(false),
	  lmb_down(false),
	  mmb_down(false),
	  rmb_down(false),
	  m_translation(mat4()),
	  m_rotation(mat4()),
	  m_rotateX(0),
	  m_rotateY(0),
	  m_picking(false),
	  m_jointRotateX(0),
	  m_jointRotateY(0)
{

}

//----------------------------------------------------------------------------------------
// Destructor
A3::~A3()
{

}

//----------------------------------------------------------------------------------------
/*
 * Called once, at program start.
 */
void A3::init()
{
	// Set the background colour.
	glClearColor(0.35, 0.35, 0.35, 1.0);

	createShaderProgram();

	glGenVertexArrays(1, &m_vao_arcCircle);
	glGenVertexArrays(1, &m_vao_meshData);
	enableVertexShaderInputSlots();

	processLuaSceneFile(m_luaSceneFile);

	// Load and decode all .obj files at once here.  You may add additional .obj files to
	// this list in order to support rendering additional mesh types.  All vertex
	// positions, and normals will be extracted and stored within the MeshConsolidator
	// class.
	unique_ptr<MeshConsolidator> meshConsolidator (new MeshConsolidator{
			getAssetFilePath("cube.obj"),
			getAssetFilePath("sphere.obj"),
			getAssetFilePath("suzanne.obj")
	});


	// Acquire the BatchInfoMap from the MeshConsolidator.
	meshConsolidator->getBatchInfoMap(m_batchInfoMap);

	// Take all vertex data within the MeshConsolidator and upload it to VBOs on the GPU.
	uploadVertexDataToVbos(*meshConsolidator);

	mapVboDataToVertexShaderInputLocations();

	initPerspectiveMatrix();

	initViewMatrix();

	initLightSources();


	// Exiting the current scope calls delete automatically on meshConsolidator freeing
	// all vertex data resources.  This is fine since we already copied this data to
	// VBOs on the GPU.  We have no use for storing vertex data on the CPU side beyond
	// this point.
}

//----------------------------------------------------------------------------------------
void A3::processLuaSceneFile(const std::string & filename) {
	// This version of the code treats the Lua file as an Asset,
	// so that you'd launch the program with just the filename
	// of a puppet in the Assets/ directory.
	// std::string assetFilePath = getAssetFilePath(filename.c_str());
	// m_rootNode = std::shared_ptr<SceneNode>(import_lua(assetFilePath));

	// This version of the code treats the main program argument
	// as a straightforward pathname.
	m_rootNode = std::shared_ptr<SceneNode>(import_lua(filename));
	if (!m_rootNode) {
		std::cerr << "Could not open " << filename << std::endl;
	}
}

//----------------------------------------------------------------------------------------
void A3::createShaderProgram()
{
	m_shader.generateProgramObject();
	m_shader.attachVertexShader( getAssetFilePath("VertexShader.vs").c_str() );
	m_shader.attachFragmentShader( getAssetFilePath("FragmentShader.fs").c_str() );
	m_shader.link();

	m_shader_arcCircle.generateProgramObject();
	m_shader_arcCircle.attachVertexShader( getAssetFilePath("arc_VertexShader.vs").c_str() );
	m_shader_arcCircle.attachFragmentShader( getAssetFilePath("arc_FragmentShader.fs").c_str() );
	m_shader_arcCircle.link();
}

//----------------------------------------------------------------------------------------
void A3::enableVertexShaderInputSlots()
{
	//-- Enable input slots for m_vao_meshData:
	{
		glBindVertexArray(m_vao_meshData);

		// Enable the vertex shader attribute location for "position" when rendering.
		m_positionAttribLocation = m_shader.getAttribLocation("position");
		glEnableVertexAttribArray(m_positionAttribLocation);

		// Enable the vertex shader attribute location for "normal" when rendering.
		m_normalAttribLocation = m_shader.getAttribLocation("normal");
		glEnableVertexAttribArray(m_normalAttribLocation);

		CHECK_GL_ERRORS;
	}


	//-- Enable input slots for m_vao_arcCircle:
	{
		glBindVertexArray(m_vao_arcCircle);

		// Enable the vertex shader attribute location for "position" when rendering.
		m_arc_positionAttribLocation = m_shader_arcCircle.getAttribLocation("position");
		glEnableVertexAttribArray(m_arc_positionAttribLocation);

		CHECK_GL_ERRORS;
	}

	// Restore defaults
	glBindVertexArray(0);
}

//----------------------------------------------------------------------------------------
void A3::uploadVertexDataToVbos (
		const MeshConsolidator & meshConsolidator
) {
	// Generate VBO to store all vertex position data
	{
		glGenBuffers(1, &m_vbo_vertexPositions);

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo_vertexPositions);

		glBufferData(GL_ARRAY_BUFFER, meshConsolidator.getNumVertexPositionBytes(),
				meshConsolidator.getVertexPositionDataPtr(), GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		CHECK_GL_ERRORS;
	}

	// Generate VBO to store all vertex normal data
	{
		glGenBuffers(1, &m_vbo_vertexNormals);

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo_vertexNormals);

		glBufferData(GL_ARRAY_BUFFER, meshConsolidator.getNumVertexNormalBytes(),
				meshConsolidator.getVertexNormalDataPtr(), GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		CHECK_GL_ERRORS;
	}

	// Generate VBO to store the trackball circle.
	{
		glGenBuffers( 1, &m_vbo_arcCircle );
		glBindBuffer( GL_ARRAY_BUFFER, m_vbo_arcCircle );

		float *pts = new float[ 2 * CIRCLE_PTS ];
		for( size_t idx = 0; idx < CIRCLE_PTS; ++idx ) {
			float ang = 2.0 * M_PI * float(idx) / CIRCLE_PTS;
			pts[2*idx] = cos( ang );
			pts[2*idx+1] = sin( ang );
		}

		glBufferData(GL_ARRAY_BUFFER, 2*CIRCLE_PTS*sizeof(float), pts, GL_STATIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		CHECK_GL_ERRORS;
	}
}

//----------------------------------------------------------------------------------------
void A3::mapVboDataToVertexShaderInputLocations()
{
	// Bind VAO in order to record the data mapping.
	glBindVertexArray(m_vao_meshData);

	// Tell GL how to map data from the vertex buffer "m_vbo_vertexPositions" into the
	// "position" vertex attribute location for any bound vertex shader program.
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo_vertexPositions);
	glVertexAttribPointer(m_positionAttribLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

	// Tell GL how to map data from the vertex buffer "m_vbo_vertexNormals" into the
	// "normal" vertex attribute location for any bound vertex shader program.
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo_vertexNormals);
	glVertexAttribPointer(m_normalAttribLocation, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

	//-- Unbind target, and restore default values:
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	CHECK_GL_ERRORS;

	// Bind VAO in order to record the data mapping.
	glBindVertexArray(m_vao_arcCircle);

	// Tell GL how to map data from the vertex buffer "m_vbo_arcCircle" into the
	// "position" vertex attribute location for any bound vertex shader program.
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo_arcCircle);
	glVertexAttribPointer(m_arc_positionAttribLocation, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	//-- Unbind target, and restore default values:
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	CHECK_GL_ERRORS;
}

//----------------------------------------------------------------------------------------
void A3::initPerspectiveMatrix()
{
	float aspect = ((float)m_windowWidth) / m_windowHeight;
	m_perpsective = glm::perspective(degreesToRadians(60.0f), aspect, 0.1f, 100.0f);
}


//----------------------------------------------------------------------------------------
void A3::initViewMatrix() {
	m_view = glm::lookAt(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f),
			vec3(0.0f, 1.0f, 0.0f));
}

//----------------------------------------------------------------------------------------
void A3::initLightSources() {
	// World-space position
	m_light.position = vec3(-2.0f, 5.0f, 0.5f);
	m_light.rgbIntensity = vec3(0.8f); // White light
}

//----------------------------------------------------------------------------------------
void A3::uploadCommonSceneUniforms() {
	m_shader.enable();
	{
		//-- Set Perpsective matrix uniform for the scene:
		GLint location = m_shader.getUniformLocation("Perspective");
		glUniformMatrix4fv(location, 1, GL_FALSE, value_ptr(m_perpsective));
		CHECK_GL_ERRORS;

		location = m_shader.getUniformLocation("picking");
		glUniform1i( location, m_picking ? 1 : 0 );
		CHECK_GL_ERRORS;

		if (!m_picking){
			//-- Set LightSource uniform for the scene:
			{
				location = m_shader.getUniformLocation("light.position");
				glUniform3fv(location, 1, value_ptr(m_light.position));
				location = m_shader.getUniformLocation("light.rgbIntensity");
				glUniform3fv(location, 1, value_ptr(m_light.rgbIntensity));
				CHECK_GL_ERRORS;
			}

			//-- Set background light ambient intensity
			{
				location = m_shader.getUniformLocation("ambientIntensity");
				vec3 ambientIntensity(0.05f);
				glUniform3fv(location, 1, value_ptr(ambientIntensity));
				CHECK_GL_ERRORS;
			}
		}
	}
	m_shader.disable();
}

//----------------------------------------------------------------------------------------
/*
 * Called once per frame, before guiLogic().
 */
void A3::appLogic()
{
	// Place per frame, application logic here ...

	uploadCommonSceneUniforms();
}

//----------------------------------------------------------------------------------------
/*
 * Called once per frame, after appLogic(), but before the draw() method.
 */
void A3::guiLogic()
{
	if( !show_gui ) {
		return;
	}

	static bool firstRun(true);
	if (firstRun) {
		ImGui::SetNextWindowPos(ImVec2(50, 50));
		firstRun = false;
	}

	static bool showDebugWindow(true);
	ImGuiWindowFlags windowFlags(ImGuiWindowFlags_AlwaysAutoResize);
	float opacity(0.5f);

	ImGui::Begin("Application", &showDebugWindow, ImVec2(100,100), opacity,
			windowFlags);

		// Create Button, and check if it was clicked:
		if( ImGui::Button( "Quit Application" ) ) {
			glfwSetWindowShouldClose(m_window, GL_TRUE);
		}

		if( ImGui::Button( "Reset Position" ) ) {
			resetPosition();
		}

		if( ImGui::Button( "Reset Orientation" ) ) {
			resetOrientation();
		}

		if( ImGui::Button( "Reset Joints" ) ) {
			resetJoints();
		}

		if( ImGui::Button( "Reset All" ) ) {
			resetAll();
		}

		ImGui::Text( "Framerate: %.1f FPS", ImGui::GetIO().Framerate );

	ImGui::End();

	ImGui::Begin("Edit", &showDebugWindow, ImVec2(100,100), opacity,
			windowFlags);
		if( ImGui::Button( "Undo" ) ) {
			undo();
		}

		if( ImGui::Button( "Redo" ) ) {
			redo();
		}

	ImGui::End();

	ImGui::Begin("Options", &showDebugWindow, ImVec2(100,100), opacity,
			windowFlags);
			
		if( ImGui::Checkbox( "Circle", &m_drawCircle) ) {
			
		}

		if( ImGui::Checkbox( "Z-buffer", &m_zbuffer) ) {
			
		}

		if( ImGui::Checkbox( "Backface Culling", &m_backfaceCulling) ) {
			
		}
		
		if( ImGui::Checkbox( "Frontface Culling", &m_frontfaceCulling) ) {
			
		}


		if( ImGui::RadioButton( "Position/Orientation", &tempMode, 0 ) ) {
			m_mode = POSITION;
		}

		if( ImGui::RadioButton( "Joints", &tempMode, 1 ) ) {
			m_mode = JOINT;
		}
	ImGui::End();

	ImGui::Begin("Temp Joint Picker", &showDebugWindow, ImVec2(100,100), opacity,
			windowFlags);

		jointPickerGui((SceneNode*) &(*m_rootNode));
			

	ImGui::End();
	
}

//makes a list of joints to pick in case no time for picking
void A3::jointPickerGui(SceneNode *node){
	if (node == nullptr) return;
	if (node->m_nodeType == NodeType::JointNode) {
		if( ImGui::Checkbox( node->m_name.c_str() , &(node->isSelected))) {
			if (node->isSelected) {
				cout << "selected " << *node << endl;
				m_selectedJoints.emplace_back(node);
				node->selectChild();
			} else {
				auto it = std::find(m_selectedJoints.begin(), m_selectedJoints.end(), node);
				if (it != m_selectedJoints.end()){
					cout << "deselected " << *node << endl;
					node->selectChild();
					m_selectedJoints.erase(it);
				}
			}
		}
	}

	for (SceneNode *child : node->children){
		jointPickerGui(child);
	}
}

//----------------------------------------------------------------------------------------
// Update mesh specific shader uniforms:
static void updateShaderUniforms(
		const ShaderProgram & shader,
		const GeometryNode & node,
		const glm::mat4 & viewMatrix, bool picking
) {

	shader.enable();
	{
		//-- Set ModelView matrix:
		GLint location = shader.getUniformLocation("ModelView");
		mat4 modelView = viewMatrix * node.trans;
		glUniformMatrix4fv(location, 1, GL_FALSE, value_ptr(modelView));
		CHECK_GL_ERRORS;

		if (picking){
			//cout << "picking " << node << endl;
			int idx = node.m_nodeId;
			float r = float(idx&0xff) / 255.0f;
			float g = float((idx>>8)&0xff) / 255.0f;
			float b = float((idx>>16)&0xff) / 255.0f;

			location = shader.getUniformLocation("material.kd");
			glUniform3f( location, r, g, b );
			CHECK_GL_ERRORS;

			location = shader.getUniformLocation("material.ks");
			glUniform3f( location, r, g, b );
			CHECK_GL_ERRORS;

			location = shader.getUniformLocation("material.shininess");
			glUniform1f(location, node.material.shininess);
			CHECK_GL_ERRORS;
		
		} else {
			//-- Set NormMatrix:
			location = shader.getUniformLocation("NormalMatrix");
			mat3 normalMatrix = glm::transpose(glm::inverse(mat3(modelView)));
			glUniformMatrix3fv(location, 1, GL_FALSE, value_ptr(normalMatrix));
			CHECK_GL_ERRORS;


			//-- Set Material values:
			location = shader.getUniformLocation("material.kd");
			vec3 kd = node.isSelected ? glm::vec3(1.0f) : node.material.kd;
			glUniform3fv(location, 1, value_ptr(kd));
			CHECK_GL_ERRORS;
			location = shader.getUniformLocation("material.ks");
			vec3 ks = node.isSelected ? glm::vec3(1.0f) : node.material.ks;
			glUniform3fv(location, 1, value_ptr(ks));
			CHECK_GL_ERRORS;
			location = shader.getUniformLocation("material.shininess");
			glUniform1f(location, node.isSelected ? 0.0f :node.material.shininess);
			CHECK_GL_ERRORS;
		}

	}
	shader.disable();

}

//----------------------------------------------------------------------------------------
/*
 * Called once per frame, after guiLogic().
 */
void A3::draw() {

	m_view = m_translation * m_rotation * glm::lookAt(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f),
			vec3(0.0f, 1.0f, 0.0f));

	if (m_zbuffer)
		glEnable( GL_DEPTH_TEST );

	if (m_backfaceCulling || m_frontfaceCulling){
		glEnable(GL_CULL_FACE);
		if (m_backfaceCulling && m_frontfaceCulling){
			glCullFace(GL_FRONT_AND_BACK);
		} else if (m_backfaceCulling){
			glCullFace(GL_BACK);
		} else if (m_frontfaceCulling){
			glCullFace(GL_FRONT);
		}
	}

	renderSceneGraph(*m_rootNode);

	if (m_zbuffer)
		glDisable( GL_DEPTH_TEST );

	if (m_backfaceCulling || m_frontfaceCulling){
		glDisable(GL_CULL_FACE);
	}
	if (m_drawCircle)
		renderArcCircle();
}

//----------------------------------------------------------------------------------------
void A3::renderSceneGraph(const SceneNode & root) {

	// Bind the VAO once here, and reuse for all GeometryNode rendering below.
	glBindVertexArray(m_vao_meshData);

	// This is emphatically *not* how you should be drawing the scene graph in
	// your final implementation.  This is a non-hierarchical demonstration
	// in which we assume that there is a list of GeometryNodes living directly
	// underneath the root node, and that we can draw them in a loop.  It's
	// just enough to demonstrate how to get geometry and materials out of
	// a GeometryNode and onto the screen.

	// You'll want to turn this into recursive code that walks over the tree.
	// You can do that by putting a method in SceneNode, overridden in its
	// subclasses, that renders the subtree rooted at every node.  Or you
	// could put a set of mutually recursive functions in this class, which
	// walk down the tree from nodes of different types.

	renderNodes((SceneNode *) &root);

	glBindVertexArray(0);
	CHECK_GL_ERRORS;
}

void A3::renderNodes(SceneNode *root, bool picking){

	if (root->m_nodeType == NodeType::GeometryNode){
		const GeometryNode * geometryNode = static_cast<const GeometryNode *>(root);

		updateShaderUniforms(m_shader, *geometryNode, m_view, m_picking);

		BatchInfo batchInfo = m_batchInfoMap[geometryNode->meshId];

		//-- Now render the mesh:
		m_shader.enable();
		glDrawArrays(GL_TRIANGLES, batchInfo.startIndex, batchInfo.numIndices);

		m_shader.disable();
	}
	for (SceneNode *child : root->children){
		child->set_transform(root->get_transform() * child->get_transform());
		renderNodes(child);
		child->set_transform(glm::inverse(root->get_transform()) * child->get_transform());
	}
}

//----------------------------------------------------------------------------------------
// Draw the trackball circle.
void A3::renderArcCircle() {
	glBindVertexArray(m_vao_arcCircle);

	m_shader_arcCircle.enable();
		GLint m_location = m_shader_arcCircle.getUniformLocation( "M" );
		float aspect = float(m_framebufferWidth)/float(m_framebufferHeight);
		glm::mat4 M;
		if( aspect > 1.0 ) {
			M = glm::scale( glm::mat4(), glm::vec3( 0.5/aspect, 0.5, 1.0 ) );
		} else {
			M = glm::scale( glm::mat4(), glm::vec3( 0.5, 0.5*aspect, 1.0 ) );
		}

		M = m_translation * M;
		glUniformMatrix4fv( m_location, 1, GL_FALSE, value_ptr( M ) );
		glDrawArrays( GL_LINE_LOOP, 0, CIRCLE_PTS );
	m_shader_arcCircle.disable();

	glBindVertexArray(0);
	CHECK_GL_ERRORS;
}

void A3::resetOrientation(){
	m_rotation = mat4();
}

void A3::resetPosition(){
	m_translation = mat4();
}

//reset& deselects joints, have to manually clear selection vector
void A3::resetJoints(){	
	while(!m_undoStack.empty()){
		Command* cmd = m_undoStack.back();
		m_undoStack.pop_back();
		cmd->execute(-1);
	}
	std::vector<SceneNode*>::iterator it = m_selectedJoints.begin();
	for (it; it != m_selectedJoints.end(); ++it){
		JointNode * jointNode = static_cast<JointNode *>(*it);
		jointNode->resetJoint();
	}
	deselectJoints((SceneNode*)&*m_rootNode);
	m_selectedJoints.clear();
	m_undoStack.clear();
	m_redoStack.clear();
}

void A3::deselectJoints(SceneNode *root){	
	root->isSelected = false;
	for (SceneNode *child : root->children){
		deselectJoints(child);
	}
}

void A3::resetAll(){
	resetOrientation();
	resetPosition();
	resetJoints();
}

//----------------------------------------------------------------------------------------
/*
 * Called once, after program is signaled to terminate.
 */
void A3::cleanup()
{

}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles cursor entering the window area events.
 */
bool A3::cursorEnterWindowEvent (
		int entered
) {
	bool eventHandled(false);

	// Fill in with event handling code...

	return eventHandled;
}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles mouse cursor movement events.
 */
bool A3::mouseMoveEvent (
		double xPos,
		double yPos
) {
	bool eventHandled(false);

	if (ImGui::IsMouseHoveringAnyWindow()) return eventHandled;

	float deltaX = xPos - m_mouseX;
	float deltaY = m_mouseY - yPos;
	float modifier = 0.001;

	if (m_mode == POSITION){
		if (lmb_down){
			mat4 _trans = glm::translate(m_translation, vec3(modifier*deltaX, modifier*deltaY, 0));
			m_translation = _trans;
		} 
		if (mmb_down){
			m_translation = glm::translate(m_translation, vec3(0, 0, modifier*deltaY));
		} 

		if (rmb_down){
			float w = m_framebufferWidth/2;
			float h = m_framebufferHeight/2;
			float d = std::min(w,h);
			float vecX, vecY, vecZ;
			
			float aspect = (float)m_framebufferWidth/(float)m_framebufferHeight;

			vec3 rotvec = vCalcRotVec((xPos - w), (h - yPos),
						(m_mouseX - w), (h - m_mouseY),
						d);
			mat4 rot = vAxisRotMatrix(rotvec[0], rotvec[1], rotvec[2]);

			m_rotation =  rot * m_rotation;

		}

	} else if (m_mode == JOINT){
		moveJoints((SceneNode *) &m_rootNode, deltaX, deltaY);
	}

	m_mouseX = xPos;
	m_mouseY = yPos;
	return eventHandled;
}

void A3::moveJoints(SceneNode *root, float x, float y){
	if (mmb_down){
		std::vector<SceneNode*>::iterator it = m_selectedJoints.begin();
		for (it; it != m_selectedJoints.end(); ++it){
			JointNode * jointNode = static_cast<JointNode *>(*it);
			if((*it)->m_name == "leftElbow-hand" 
				|| (*it)->m_name == "rightElbow-hand"
				|| (*it)->m_name ==  "leftArm-elbow"
				|| (*it)->m_name ==  "rightArm-elbow"){
				
				jointNode->rotate('y', x);
			} else {
				jointNode->rotate('x', x);
			}
			
		}
	}

	if (rmb_down){
		//m_curCmd->_neckY += y;
		std::vector<SceneNode*>::iterator it = m_selectedJoints.begin();
		for (it; it != m_selectedJoints.end(); ++it){
			if ((*it)->m_name == "neckJoint"){
				JointNode * jointNode = static_cast<JointNode *>(*it);
				jointNode->rotate('y', y);
			}
		}
	}
}

void A3::select(SceneNode *node){
	if (node->isSelected) {
		cout << "selected " << *node << endl;
		m_selectedJoints.emplace_back(node);
	} else {
		auto it = std::find(m_selectedJoints.begin(), m_selectedJoints.end(), node);
		if (it != m_selectedJoints.end()){
			cout << "deselected " << *node << endl;
			m_selectedJoints.erase(it);
		}
	}

}

void A3::undo(){
	if (m_undoStack.empty()) return;

	Command* cmd = m_undoStack.back();
	m_undoStack.pop_back();
	cmd->execute(-1);
	m_redoStack.emplace_back(cmd);
}

void A3::redo(){
	if (m_redoStack.empty()) return;

	Command* cmd = m_redoStack.back();
	m_redoStack.pop_back();
	cmd->execute(1);
	m_undoStack.emplace_back(cmd);

}

//picking code
void A3::pick(SceneNode *node, unsigned int id){
	//do picking in joint 
	if (node->m_nodeType == NodeType::JointNode){
		if (node->m_nodeId == id){
			node->isSelected = !(node->isSelected);
			node->selectChild();
			select(node);
			return;
		} else {
			for (SceneNode* child : node->children){
				if (child->m_nodeId == id){
					//if child is to be selected, select parent joint as well
					node->isSelected = !(node->isSelected);
					node->selectChild();
					select(node);
					return;
				}	
			}
		}

	} 

	for (SceneNode* child : node->children){
		pick(child, id);	
	}

}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles mouse button events.
 */
bool A3::mouseButtonInputEvent (
		int button,
		int actions,
		int mods
) {
	bool eventHandled(false);

	if (ImGui::IsMouseHoveringAnyWindow()) return eventHandled;
	// Fill in with event handling code...
	if (actions == GLFW_PRESS){
		if (button == GLFW_MOUSE_BUTTON_LEFT){
			if (m_mode == JOINT){
				//picking code from PickingExample.cpp
				m_picking = true;
				double xpos, ypos;
				glfwGetCursorPos( m_window, &xpos, &ypos );
				uploadCommonSceneUniforms();

				glClearColor(1.0, 1.0, 1.0, 1.0 );
				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
				glClearColor(0.35, 0.35, 0.35, 1.0);

				draw();

				CHECK_GL_ERRORS;

				xpos *= double(m_framebufferWidth) / double(m_windowWidth);
				ypos = m_windowHeight - ypos;
				ypos *= double(m_framebufferHeight) / double(m_windowHeight);

				GLubyte buffer[ 4 ] = { 0, 0, 0, 0 };

				glReadBuffer( GL_BACK );
				glReadPixels( int(xpos), int(ypos), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, buffer );
				CHECK_GL_ERRORS;

				unsigned int what = buffer[0] + (buffer[1] << 8) + (buffer[2] << 16);

				pick((SceneNode*)&(*m_rootNode), what);

				CHECK_GL_ERRORS;

				m_picking = false;
			} else {
				lmb_down = true;
			}
		} else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
			mmb_down = true;
			if (!cmd_started && m_mode == JOINT) { 
				cmd_started = true;
				m_curCmd = new Command();
				for (int i = 0; i < m_selectedJoints.size(); i++){
					m_selectedJoints[i]->start_undo();
					m_curCmd->_nodes.push_back(m_selectedJoints[i]);
				}
				
			}
		} else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
			rmb_down = true;
			if (!cmd_started && m_mode == JOINT) { 
				cmd_started = true;
				m_curCmd = new Command();
				for (int i = 0; i < m_selectedJoints.size(); i++){
					m_selectedJoints[i]->start_undo();
					m_curCmd->_nodes.push_back(m_selectedJoints[i]);
				}
			}
		}
	}

	if (actions == GLFW_RELEASE){
		if (button == GLFW_MOUSE_BUTTON_LEFT){
			lmb_down = false;
		} else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
			mmb_down = false;
			if (!rmb_down && cmd_started && m_mode == JOINT){
				cmd_started = false;
				//push rotation matrices for each selected joint onto undo cmd
				for (int i = 0; i < m_selectedJoints.size(); i++){
					m_curCmd->_mats.push_back(m_selectedJoints[i]->end_undo());
				}
				m_undoStack.push_back(m_curCmd);
			}
		} else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
			rmb_down = false;
			if (!mmb_down && cmd_started && m_mode == JOINT){
				cmd_started = false;
				//push rotation matrices for each selected joint onto undo cmd
				for (int i = 0; i < m_selectedJoints.size(); i++){
					m_curCmd->_mats.push_back(m_selectedJoints[i]->end_undo());
				}
				m_undoStack.push_back(m_curCmd);
			}
		}
	}

	return eventHandled;
}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles mouse scroll wheel events.
 */
bool A3::mouseScrollEvent (
		double xOffSet,
		double yOffSet
) {
	bool eventHandled(false);

	// Fill in with event handling code...

	return eventHandled;
}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles window resize events.
 */
bool A3::windowResizeEvent (
		int width,
		int height
) {
	bool eventHandled(false);
	initPerspectiveMatrix();
	return eventHandled;
}

//----------------------------------------------------------------------------------------
/*
 * Event handler.  Handles key input events.
 */
bool A3::keyInputEvent (
		int key,
		int action,
		int mods
) {
	bool eventHandled(false);

	if( action == GLFW_PRESS ) {
		if( key == GLFW_KEY_M ) {
			show_gui = !show_gui;
			eventHandled = true;
		} else if (key == GLFW_KEY_Q) {
			glfwSetWindowShouldClose(m_window, GL_TRUE);
		} else if (key == GLFW_KEY_O){
			resetOrientation();
		} else if (key == GLFW_KEY_I){
			resetPosition();
		} else if (key == GLFW_KEY_N){
			resetJoints();
		} else if (key == GLFW_KEY_A){
			resetAll();
		} else if (key == GLFW_KEY_U){
			undo();
		} else if (key == GLFW_KEY_R){
			redo();
		} else if (key == GLFW_KEY_C){
			m_drawCircle = !m_drawCircle;
		} else if (key == GLFW_KEY_Z){
			m_zbuffer = !m_zbuffer;
		} else if (key == GLFW_KEY_B){
			m_backfaceCulling = !m_backfaceCulling;
		} else if (key == GLFW_KEY_F){
			m_frontfaceCulling = !m_frontfaceCulling;
		} else if (key == GLFW_KEY_P){
			m_mode = POSITION;
			tempMode = 0;
		} else if (key == GLFW_KEY_J){
			m_mode = JOINT;
			tempMode = 1;
		}
	}
	// Fill in with event handling code...

	return eventHandled;
}
