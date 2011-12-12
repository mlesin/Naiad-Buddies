// ----------------------------------------------------------------------------
//
// Mesh-Rib-Write.h
//
// A Naiad file operator to export Mesh bodies as RIB archive.
//
// Copyright (c) 2011 Exotic Matter AB.  All rights reserved.
//
// This material  contains the  confidential and proprietary information of
// Exotic  Matter  AB  and  may  not  be  modified,  disclosed,  copied  or 
// duplicated in any form,  electronic or hardcopy,  in whole or  in  part, 
// without the express prior written consent of Exotic Matter AB.
//
// This copyright notice does not imply publication.
//
//    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
//    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,  INCLUDING,  BUT NOT 
//    LIMITED TO,  THE IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS
//    FOR  A  PARTICULAR  PURPOSE  ARE DISCLAIMED.  IN NO EVENT SHALL THE
//    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//    BUT  NOT LIMITED TO,  PROCUREMENT OF SUBSTITUTE GOODS  OR  SERVICES; 
//    LOSS OF USE,  DATA,  OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER
//    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,  STRICT
//    LIABILITY,  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN
//    ANY  WAY OUT OF THE USE OF  THIS SOFTWARE,  EVEN IF ADVISED OF  THE
//    POSSIBILITY OF SUCH DAMAGE.
//
// ----------------------------------------------------------------------------

#include "ri.h"

#include <Ni.h>

#include <NgBodyOp.h>
#include <NgProjectPath.h>

#include <NbBlock.h>
#include <NbFilename.h>
#include <NbFileIo.h>

class Mesh_Rib_Write : public Ng::BodyOp
{
public:
    Mesh_Rib_Write(const Nb::String& name)
        : Ng::BodyOp(name) {}

    ~Mesh_Rib_Write() {}

    virtual Nb::String
    typeName() const
    { return "Mesh-Rib-Write"; }

    virtual bool
    enabled(const Nb::TimeBundle& tb) const
    { 
        if(!tb.frameStep && param1e("Output Timesteps")->eval(tb)=="Off")
            return false;
        return Ng::BodyOp::enabled(tb);
    }

    virtual void
    preStep(const Nb::TimeBundle& tb) 
    {
        // open RIB archive for writing...

        _fileName =
            Nb::sequenceToFilename(
                Ng::projectPath(),
                param1s("RIB Archive")->eval(tb),
                tb.frame,
                tb.frameStep ? -1 : tb.timestep,
                param1i("Frame Padding")->eval(tb)
                );

        // ensure output path exists before opening the file
        const Nb::String path = Nb::extractPath(_fileName);
        Nb::mkdirp(path);

        // figure out binary compression or not..
        const bool binaryRib=
            (param1e("RIB Format")->eval(tb)=="Binary (Compressed)");

        if(binaryRib) {
            RtString compression[1] = { "gzip" };
            RiOption("rib", "compression", (RtPointer) compression, RI_NULL);
        }

        // open RIB...
        RiBegin((RtToken)_fileName.c_str());

        // Set output format to binary
        if(binaryRib) {
            RtString format[1] = {"binary"};
            RiOption( "rib", "format", (RtPointer)format, RI_NULL );
        } else {
            RtString format[1] = {"ascii"};
            RiOption( "rib", "format", (RtPointer)format, RI_NULL );
        }

        _particleCount = 0;
    }

    virtual void
    stepAdmittedBody(Nb::Body*             body, 
                     Ng::NelContext&       nelContext, 
                     const Nb::TimeBundle& tb)
    {
    	const Nb::String bodyNameList = param1s("Body Names")->eval(tb);

        // skip bodies not listed in "Body Names" ...
        if(!body->name().listed_in(bodyNameList))
            return;

        const bool motionBlur=
            (param1e("Motion Blur")->eval(tb)=="On");
        std::vector<Nb::String> motionSamplesAscii =
            param1s("Motion Samples")->eval(tb).to_vector();
        if(motionBlur && motionSamplesAscii.size()<2)
            NB_THROW("Must specify two or more motion samples for motion-blur");
        em::array1f motionSamples(motionSamplesAscii.size());
        for(int i=0; i<motionSamplesAscii.size(); ++i)
            motionSamples(i)=atof(motionSamplesAscii[i].c_str());

        // grab the tile-layout
        const Nb::TileLayout& layout = 
            body->constLayout();

        // grab the particle shape/channels
        const Nb::ParticleShape& particle = 
            body->constParticleShape();
        const Nb::FieldShape* field = 
            body->queryConstFieldShape();
        const Nb::BlockArray3f& xBlocks = 
            particle.constBlocks3f("position");
        
        // velocity for motion-blur
        const Nb::BlockArray3f* uBlocks = 
            particle.queryConstBlocks3f("velocity");        
        const Nb::Field1f* u = field ? 
            field->queryConstField3f("velocity",0) : 0;
        const Nb::Field1f* v = field ? 
            field->queryConstField3f("velocity",1) : 0;
        const Nb::Field1f* w = field ? 
            field->queryConstField3f("velocity",2) : 0;

        // velocity source
        const bool pvelSrc = 
            (param1e("Velocity Source")->eval(tb) == "Particle.velocity");

        if(motionBlur) {
            if(pvelSrc && !uBlocks)
                NB_THROW("Motion-blur setting requires Particle.velocity");
            if(!pvelSrc && (!u || !v || !w))
                NB_THROW("Motion-blur setting requires Field.velocity");
        }
       


    }

    virtual void
    postStep(const Nb::TimeBundle& tb) 
    {
        RiEnd();
        
        NB_INFO("Wrote " << _particleCount << " particles to '" << _fileName 
                << "'");
    }

private:
    void
    _outputParticles()
    {
        // per-particle radius
        const Nb::BlockArray1f* rBlocks = 
            particle.queryConstBlocks1f(
                param1s("Per-Particle Radius Channel")->eval(tb)
                );
        const float constantRadius = 
            param1f("Constant Radius")->eval(tb);
        const bool ppRadius =
            (param1e("Per-Particle Radius")->eval(tb)=="On");
        if(ppRadius && !rBlocks)
            NB_THROW("Per-Particle Radius channel '" << 
                     param1s("Per-Particle Radius Channel")->eval(tb) << "' " <<
                     "not found");

        // output to RIB

        const char* width = ppRadius ? "width" : "constantwidth";   
        RtToken tokens[2] = { (RtToken)"P", (RtToken)width };

        // write points
        const int bcount=xBlocks.block_count();
        for(int b=0; b<bcount; ++b) {
            const Nb::Block3f& xb = xBlocks(b);
            const int pcount = xb.size();
            if(pcount==0) 
                continue;
            if(!motionBlur) {
                RtPointer data[2] = { (RtPointer)xb.data(), 
                                      ppRadius ? 
                                      (RtPointer)(*rBlocks)(b).data() :
                                      (RtPointer)&constantRadius };
                RiPointsV(pcount, 2, tokens, data);                
            } else {
                em::array1vec3f xt(pcount);
                RiMotionBeginV(motionSamples.size(),
                               motionSamples.data);
                for(int s=0; s<motionSamples.size(); ++s) {
                    if(pvelSrc) { // Particle.velocity
                        const float dt = (float)(tb.frame_dt*motionSamples(s));
                        const Nb::Block3f& ub = (*uBlocks)(b);
#pragma omp parallel for
                        for(int p=0; p<pcount; ++p)
                            xt(p) = xb(p) + ub(p)*dt;
                    } else { // Field.velocity
#pragma omp parallel for schedule(dynamic)
                        for(int p=0; p<pcount; ++p) {
                            Nb::Vec3f& xp = xt(p);
                            float dt;
                            if(s==0) {
                                dt = (float)(tb.frame_dt*motionSamples(s));
                                xp = xb(p);
                            } else {
                                dt = (float)(tb.frame_dt*(motionSamples(s)-
                                                          motionSamples(s-1)));
                            }                            
                            const float ux = 
                                Nb::sampleFieldCubic1f(xp,layout,*u);
                            const float vx = 
                                Nb::sampleFieldCubic1f(xp,layout,*v);
                            const float wx = 
                                Nb::sampleFieldCubic1f(xp,layout,*w);
                            xp += Nb::Vec3f(ux,vx,wx)*dt;
                        }                        
                    }
                    RtPointer data[2] = { (RtPointer)xt.data, 
                                          ppRadius ? 
                                          (RtPointer)(*rBlocks)(b).data() :
                                          (RtPointer)&constantRadius };
                    RiPointsV(pcount, 2, tokens, data);                
                }
                RiMotionEnd();
            }
        }

        _particleCount += particle.size();
    }

    void
    _outputMesh()
    {        
        const Nb::TriangleShape& triangle = body->constTriangleShape();
        const Nb::PointShape& point = body->constPointShape();
        const Nb::Buffer3i& index = triangle.constChannel3i("index");
        const Nb::Buffer3f& x = point.constChannel3f("position");

        // all polygons are triangles!
        Nb::Array1i nvertices(index.size(), 3);

        // parameter list (positions only for now)
        RtToken   tokens[1] = { (RtToken)"P" };
        RtPointer data[1]   = { (RtPointer)x.data() };

        if(!motionBlur) {            
            RiPointsPolygons((RtInt)tcount, 
                             (RtInt*)nvertices.data(), 
                             (RtInt*)index.data(), 
                             tokens,
                             data);
        } else {
/*
            em::array1vec3f xt(pcount);

            RiMotionBeginV(motionSamples.size(),
                           motionSamples.data);

            for(int s=0; s<motionSamples.size(); ++s) {
                if(pvelSrc) { // Particle.velocity
                    const float dt = (float)(tb.frame_dt*motionSamples(s));
                    const Nb::Block3f& ub = (*uBlocks)(b);
#pragma omp parallel for
                    for(int p=0; p<pcount; ++p)
                        xt(p) = xb(p) + ub(p)*dt;
                } else { // Field.velocity
#pragma omp parallel for schedule(dynamic)
                    for(int p=0; p<pcount; ++p) {
                        Nb::Vec3f& xp = xt(p);
                        float dt;
                        if(s==0) {
                            dt = (float)(tb.frame_dt*motionSamples(s));
                            xp = xb(p);
                        } else {
                            dt = (float)(tb.frame_dt*(motionSamples(s)-
                                                      motionSamples(s-1)));
                        }                            
                        const float ux = 
                            Nb::sampleFieldCubic1f(xp,layout,*u);
                        const float vx = 
                            Nb::sampleFieldCubic1f(xp,layout,*v);
                        const float wx = 
                            Nb::sampleFieldCubic1f(xp,layout,*w);
                        xp += Nb::Vec3f(ux,vx,wx)*dt;
                    }                        
                }
                RtPointer data[2] = { (RtPointer)xt.data, 
                                      ppRadius ? 
                                      (RtPointer)(*rBlocks)(b).data() :
                                      (RtPointer)&constantRadius };
                RiPointsV(pcount, 2, tokens, data);                
            }
            RiMotionEnd();
*/
        }
    }

private:
    Nb::String _fileName;
    int64_t    _particleCount;
};

// ----------------------------------------------------------------------------

class Particle_Rib_Terminal : public Particle_Rib_Write
{
public:
    Particle_Rib_Terminal(const Nb::String& name)
        : Particle_Rib_Write(name) {}

    ~Particle_Rib_Terminal() {}

    virtual Nb::String
    typeName() const
    { return "Particle-Rib-Terminal"; }
};

// ----------------------------------------------------------------------------