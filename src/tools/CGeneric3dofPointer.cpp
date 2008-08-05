//===========================================================================
/*
    This file is part of the CHAI 3D visualization and haptics libraries.
    Copyright (C) 2003-2004 by CHAI 3D. All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License("GPL") version 2
    as published by the Free Software Foundation.

    For using the CHAI 3D libraries with software that can not be combined
    with the GNU GPL, and for taking advantage of the additional benefits
    of our support services, please contact CHAI 3D about acquiring a
    Professional Edition License.

    \author:    <http://www.chai3d.org>
    \author:    Francois Conti
    \author:    Federico Barbagli
    \version    1.1
    \date       01/2004
*/
//===========================================================================

//---------------------------------------------------------------------------
#include "CGeneric3dofPointer.h"
#include "CWorld.h"
#include <conio.h>
//---------------------------------------------------------------------------

// The radius used for proxy collision detection is equal to 
// CHAI_SCALE_PROXY_RADIUS * the radius that's rendered
#define CHAI_SCALE_PROXY_RADIUS 0.01f

//==========================================================================
/*!
      Constructor of cGeneric3dofPointer.

      \fn       cGeneric3dofPointer::cGeneric3dofPointer(cWorld* a_world)
      \param    a_world  World in which the tool will operate.
*/
//===========================================================================
cGeneric3dofPointer::cGeneric3dofPointer(cWorld* a_world)
{
    m_waitForSmallForce = true;

    // set world
    m_world = a_world;

    // set a default device for the moment
    m_device = new cGenericDevice();

    // default tool rendering settings
    m_colorDevice.set(1.0f, 0.2f, 0.0);
    m_colorProxyButtonOn.set(1.0f, 0.4f, 0.0);
    m_colorProxy.set(0.8f, 0.6f, 0.0);
    m_colorLine.set(0.7f, 0.7f, 0.7f);
    m_render_mode = RENDER_PROXY_AND_DEVICE;

    // This sets both the rendering radius and the actual proxy radius
    setRadius(0.05f);
    
    // tool frame settings
    m_showToolFrame = false;   // toggle tool frame off
    m_toolFrameSize = 0.2;      // default value for the tool frame size

    // force settings
    m_forceON = true;           // forces are ON at first
    m_forceStarted = false;
}


//==========================================================================
/*!
      Destructor of cGeneric3dofPointer.

      \fn       cGeneric3dofPointer::~cGeneric3dofPointer()
*/
//===========================================================================
cGeneric3dofPointer::~cGeneric3dofPointer()
{
    // check if device is available
    if (m_device == NULL) { return; }

    // close device driver
    m_device->close();

    delete m_device;
}


//==========================================================================
/*!
    Define a haptic device driver for this tool.

    \fn       cGeneric3dofPointer::void setDevice(cGenericDevice *a_device);

*/
//===========================================================================
void cGeneric3dofPointer::setDevice(cGenericDevice *a_device)
{
    m_device = a_device;
}


//==========================================================================
/*!
    Initialize device

    \fn       void cGeneric3dofPointer::initialize()
    \return   0 indicates success, non-zero indicates an error
*/
//===========================================================================
int cGeneric3dofPointer::initialize()
{
    // check if device is available
    if (m_device == NULL) { return -1; }

    // initialize (calibrate) device
    if (m_device->open() != 0) return -1;
    if (m_device->initialize() != 0) return -1;
    updatePose();
    if (m_device->close() != 0) return -1;

    // initialize force algorithms
    m_proxyPointForceAlgo.initialize(m_world, m_deviceGlobalPos);
    m_potentialFieldForceAlgo.initialize(m_world, m_deviceGlobalPos);

    return 0;
}


//==========================================================================
/*!
      Reset the tool.

      \fn       void cGeneric3dofPointer::start()
      \return   0 indicates success, non-zero indicates an error
*/
//===========================================================================
int cGeneric3dofPointer::start()
{
    // check if device is available
    if (m_device == NULL) { return -1; }

    // open connection to device
    return m_device->open();
}


//==========================================================================
/*!
      Stop system. Apply zero force to device

      \fn       void cGeneric3dofPointer::stop()
      \return   0 indicates success, non-zero indicates an error
*/
//===========================================================================
int cGeneric3dofPointer::stop()
{
    // check if device is available
    if (m_device == NULL) { return -1; }

    // stop the device
    return m_device->close();
}


//==========================================================================
/*!
      Update position of pointer and orientation of wrist.

      \fn       void cGeneric3dofPointer::updatePose()
*/
//===========================================================================
void cGeneric3dofPointer::updatePose()
{
    cVector3d pos;

    // check if device is available
    if (m_device == NULL) { return; }

    // read local position of device
    int result = m_device->command(CHAI_CMD_GET_POS_NORM_3D, &pos);
    if (result != 0) { return; }

    // scale  position from device into virtual workspace of tool
    m_deviceLocalPos.set( m_halfWorkspaceAxisX * pos.x,
                          m_halfWorkspaceAxisY * pos.y,
                          m_halfWorkspaceAxisZ * pos.z);

    // update global position of tool
    cVector3d tPos;
    m_globalRot.mulr(m_deviceLocalPos, tPos);
    tPos.addr(m_globalPos, m_deviceGlobalPos);

    // read orientation of wrist
    m_device->command(CHAI_CMD_GET_ROT_MATRIX, &m_deviceLocalRot);

    // update global orientation of tool
    m_deviceLocalRot.mulr(m_globalRot, m_deviceGlobalRot);

    // read switch
    m_device->command(CHAI_CMD_GET_SWITCH_0, &m_button);

    // read velocity of the device in global coordinates
    m_device->command(CHAI_CMD_GET_VEL_3D, &m_deviceLocalVel);

    // update global velocity of tool
    m_globalRot.mulr(m_deviceLocalVel, m_deviceGlobalVel);
}


//===========================================================================
/*!
      Compute the interaction forces between the tool and the virtual
      object inside the virtual world.

      \fn       void cGeneric3dofPointer::computeForces()
*/
//===========================================================================
void cGeneric3dofPointer::computeForces()
{
    // init temp variable
    cVector3d force;
    force.zero();

    // compute forces in world coordinates
    force.add(m_proxyPointForceAlgo.computeForces(m_deviceGlobalPos));
    force.add(m_potentialFieldForceAlgo.computeForces(m_deviceGlobalPos));

    // copy result
    m_lastComputedGlobalForce.copyfrom(force);
}


//==========================================================================
/*!
      Apply the latest computed force to the device.

      \fn       void cGeneric3dofPointer::applyForces()
*/
//===========================================================================
void cGeneric3dofPointer::applyForces()
{
    // check if device is available
    if (m_device == NULL) { return; }

    // convert force into device local coordinates
    cMatrix3d tRot;
    m_globalRot.transr(tRot);
    tRot.mulr(m_lastComputedGlobalForce, m_lastComputedLocalForce);

    if (
      (m_waitForSmallForce == false)
      ||
      ((!m_forceStarted) && (m_lastComputedLocalForce.lengthsq() <0.000001))
      )
        m_forceStarted = true;

    // send force to device
    if ((m_forceON) && (m_forceStarted))
     m_device->command(CHAI_CMD_SET_FORCE_3D, &m_lastComputedLocalForce);
    else
    {
        cVector3d ZeroForce = cVector3d(0.0, 0.0, 0.0);
        m_device->command(CHAI_CMD_SET_FORCE_3D, &ZeroForce);
    }
}


//==========================================================================
/*!
      Render the current tool in OpenGL.

      \fn       void cGeneric3dofPointer::render()
      \param    a_renderMode  rendering mode.
*/
//===========================================================================
void cGeneric3dofPointer::render(const int a_renderMode)
{
    // If multipass transparency is enabled, only render on a single
    // pass...
    if (a_renderMode != CHAI_RENDER_MODE_NON_TRANSPARENT_ONLY && a_renderMode != CHAI_RENDER_MODE_RENDER_ALL)
      return;   

    // render small sphere representing tip of tool
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // OpenGL matrix
    cMatrixGL frameGL;

    // compute local position of proxy
    cVector3d proxyLocalPos = m_proxyPointForceAlgo.getProxyGlobalPosition();
    proxyLocalPos.sub(m_globalPos);
    cMatrix3d tRot;
    m_globalRot.transr(tRot);
    tRot.mul(proxyLocalPos);

    if (m_render_mode != RENDER_DEVICE)
    {
      // render proxy
      if (m_button)
      {
          glColor4fv(m_colorProxyButtonOn.pColor());
      }
      else
      {
          glColor4fv(m_colorProxy.pColor());
      }

      frameGL.set(proxyLocalPos);
      frameGL.glMatrixPushMultiply();
          cDrawSphere(m_displayRadius, 16, 16);
      frameGL.glMatrixPop();
    }

    if (m_render_mode != RENDER_PROXY)
    {
        // render device position
        glColor4fv(m_colorDevice.pColor());
        frameGL.set(m_deviceLocalPos);
        frameGL.glMatrixPushMultiply();
            cDrawSphere(m_displayRadius, 16, 16);
        frameGL.glMatrixPop();

        if (m_showToolFrame)
        {
            // render device orientation
            frameGL.set(m_deviceLocalPos, m_deviceLocalRot);
            frameGL.glMatrixPushMultiply();
                cDrawFrame(m_toolFrameSize);
            frameGL.glMatrixPop();
        }
    }

    if (m_render_mode == RENDER_PROXY_AND_DEVICE)
    {
      // render line between device and proxy
      glDisable(GL_LIGHTING);
      glLineWidth(1.0);
      glColor4fv(m_colorLine.pColor());
      glBegin(GL_LINES);
          glVertex3d(m_deviceLocalPos.x, m_deviceLocalPos.y, m_deviceLocalPos.z);
          glVertex3d(proxyLocalPos.x, proxyLocalPos.y, proxyLocalPos.z);
      glEnd();
      glEnable(GL_LIGHTING);
    }    

    // Really useful debugging code for showing which triangles the proxy is
    // in contact with...

// #define RENDER_PROXY_CONTACT_TRIANGLES
#ifdef RENDER_PROXY_CONTACT_TRIANGLES
    if (m_proxyPointForceAlgo.getContactObject()) {
      
      cTriangle *t0, *t1, *t2;

      cGenericObject* obj = m_proxyPointForceAlgo.getContactObject();
      obj->computeGlobalCurrentObjectOnly();
      
      int result = m_proxyPointForceAlgo.getContacts(t0,t1,t2);
    
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      glPushMatrix();
      glLoadMatrixd(m_world->m_worldModelView);
      glBegin(GL_TRIANGLES);

      if (t0) {

        glColor3f(1.0,0,0);
        t0->getVertex0()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t0->getVertex1()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t0->getVertex2()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());

        glVertex3d(t0->getVertex0()->m_globalPos.x,t0->getVertex0()->m_globalPos.y,t0->getVertex0()->m_globalPos.z);
        glVertex3d(t0->getVertex1()->m_globalPos.x,t0->getVertex1()->m_globalPos.y,t0->getVertex1()->m_globalPos.z);
        glVertex3d(t0->getVertex2()->m_globalPos.x,t0->getVertex2()->m_globalPos.y,t0->getVertex2()->m_globalPos.z);        
      }

      if (t1) {

        glColor3f(0,1.0,0);
        t1->getVertex0()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t1->getVertex1()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t1->getVertex2()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());

        glVertex3d(t1->getVertex0()->m_globalPos.x,t1->getVertex0()->m_globalPos.y,t1->getVertex0()->m_globalPos.z);
        glVertex3d(t1->getVertex1()->m_globalPos.x,t1->getVertex1()->m_globalPos.y,t1->getVertex1()->m_globalPos.z);
        glVertex3d(t1->getVertex2()->m_globalPos.x,t1->getVertex2()->m_globalPos.y,t1->getVertex2()->m_globalPos.z);        
      }

      if (t2) {

        glColor3f(0,0,1.0);
        t2->getVertex0()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t2->getVertex1()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());
        t2->getVertex2()->computeGlobalPosition(obj->getGlobalPos(),obj->getGlobalRot());

        glVertex3d(t2->getVertex0()->m_globalPos.x,t2->getVertex0()->m_globalPos.y,t2->getVertex0()->m_globalPos.z);
        glVertex3d(t2->getVertex1()->m_globalPos.x,t2->getVertex1()->m_globalPos.y,t2->getVertex1()->m_globalPos.z);
        glVertex3d(t2->getVertex2()->m_globalPos.x,t2->getVertex2()->m_globalPos.y,t2->getVertex2()->m_globalPos.z);        
      }

      glEnd();
      glPopMatrix();
      glEnable(GL_DEPTH_TEST);
    }
#endif

}    


//==========================================================================
/*!
      Set the radius of the proxy. The value passed as parameter corresponds
      to the size of the sphere which is rendered graphically. The physical
      size of the proxy, one which collides with the triangles is set to
      CHAI_SCALE_PROXY_RADIUS * a_radius.

      \fn       void cGeneric3dofPointer::setRadius(double a_radius)
      \param    a_radius  radius of pointer.
*/
//===========================================================================
void cGeneric3dofPointer::setRadius(double a_radius)
{
    // update the radius that's rendered
    m_displayRadius = a_radius;

    // update the radius used for collision detection
    m_proxyPointForceAlgo.setProxyRadius(a_radius * CHAI_SCALE_PROXY_RADIUS);
}


//==========================================================================
/*!
      Toggles on and off the visualization of a reference frame
      located on the tool's point.

      \fn       void cGeneric3dofPointer::setToolFrame(bool a_showToolFrame, double a_toolFrameSize)
      \param    a_showToolFrame Flag which controls the tool frame display.
      \param    m_toolFrameSize Size of the tool frame.
*/
//===========================================================================
void cGeneric3dofPointer::setToolFrame(bool a_showToolFrame, double a_toolFrameSize)
{
    m_showToolFrame = a_showToolFrame;
    m_toolFrameSize = a_toolFrameSize;
}


//==========================================================================
/*!
    Turns forces ON

    \fn     void cGeneric3dofPointer::setForcesON()
    \return   0 indicates success, non-zero indicates an error

*/
//===========================================================================
int cGeneric3dofPointer::setForcesON()
{
    if (!m_forceON)
    {
          m_forceStarted = false;
          m_forceON = true;
    }
    return 0;
}


//==========================================================================
/*!
    Turns forces OFF

    \fn       void cGeneric3dofPointer::setForcesOFF()
    \return   0 indicates success, non-zero indicates an error
*/
//===========================================================================
int cGeneric3dofPointer::setForcesOFF()
{
    m_forceON = false;
    return 0;
}
