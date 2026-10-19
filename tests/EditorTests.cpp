#include "Backends.h"
#include "Geometry.h"
#include "HscCuda.h"
#include "HscCudaDevice.h"
#include "PaperRotationMath.h"
#include "HscNative.h"
#ifdef BAGUA_TEST_CUDA
#include <cuda_runtime.h>
#endif
#include "MainWindow.h"
#include "Theme.h"
#include <QAction>
#include <QApplication>
#include <QDataStream>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QQuaternion>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVector2D>
#include <QtTest>
#include <array>
#include <map>
#include <utility>
using namespace bagua;
static void moveMouse(QWidget *widget, QPoint p, Qt::MouseButtons buttons = Qt::LeftButton) {
    QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(widget->mapToGlobal(p)), Qt::NoButton, buttons,
                  Qt::NoModifier);
    QApplication::sendEvent(widget, &e);
    QApplication::processEvents();
}
class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void nativeHscWorldSmoke() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        QVERIFY2(HscNative::available(),"Build the native backend with scripts/BuildHscNative.ps1 before the full HSC tests");
        Scene scene;scene.reset(false);Object box;box.id="body";box.name="free ABD";box.position={0,2,0};box.dimensions={.4f,.4f,.4f};box.collisionEnabled=false;scene.objects={box};
        const auto authored=scene.json();HscOptions options;options.enabled=true;Physics physics;QString error;
        QVERIFY2(physics.start(scene,&error,options),qPrintable(error));QVERIFY(physics.hscActive());
        QVERIFY2(physics.step(4,&error),qPrintable(error));QVERIFY(physics.hscStats().cuda);QVERIFY(physics.hscStats().calls>=4);QVERIFY(physics.hscStats().converged);
        auto object=physics.preview.find("body");QVERIFY(object && object->mesh);QVector3D center;
        for(auto p:object->mesh->positions)center+=object->matrix().map(p);center/=float(object->mesh->positions.size());
        QVERIFY2(std::abs(center.y()-(2-9.81*Physics::timeStep*Physics::timeStep*10))<1e-4,qPrintable(QString::number(center.y(),'g',10)));
        QCOMPARE(scene.json(),authored);
#endif
    }
    void femAbdStitchCarriesLoad() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        // Articulated assemblies (vehicle bead joints, suspension springs) rely on the FEM-ABD
        // stitch carrying load. The rest length is the authored gap, so a free ABD weight must
        // hang at that gap instead of falling: 19.6 N over 20000 N/m stretches it by ~1 mm.
        Scene scene;scene.reset(false);
        Object base;base.id="base";base.name="base";base.shape=Shape::Box;base.position={0,.6f,0};base.dimensions={.4f,.4f,.4f};
        base.deformable=true;base.fixed=true;base.pinBottom=false;base.femResolution=2;base.youngModulus=20000;base.mass=8;
        base.femAttachmentBody="weight";base.attachmentStiffness=20000;
        Object weight;weight.id="weight";weight.name="weight";weight.position={0,.2f,0};weight.dimensions={.02f,.02f,.02f};
        weight.mass=2;weight.collisionEnabled=false;
        scene.objects={base,weight};
        HscOptions options;options.enabled=true;Physics physics;QString error;
        QVERIFY2(physics.start(scene,&error,options),qPrintable(error));
        QVERIFY2(physics.step(240,&error),qPrintable(error));
        auto object=physics.preview.find("weight");QVERIFY(object && object->mesh);
        float center=0;for(auto p:object->mesh->positions)center+=object->matrix().map(p).y();center/=float(object->mesh->positions.size());
        QVERIFY2(center>0.10f && center<0.30f,qPrintable(QString::number(center)));
#endif
    }
    void primitiveMeshesAreWatertight() {
        // Dynamic (ABD/IPC) bodies need a closed manifold surface. The built-in primitives must
        // weld exactly at their poles and seams, and must not emit zero-area triangles.
        for (Shape shape : {Shape::Box, Shape::Sphere, Shape::Cylinder}) {
            const auto soup = geometry(shape);
            std::map<std::array<float, 3>, int> unique;
            QVector<quint32> indices;
            for (const auto &v : soup) {
                std::array<float, 3> key{v.position.x(), v.position.y(), v.position.z()};
                auto it = unique.find(key);
                if (it == unique.end()) {
                    const int id = int(unique.size());
                    unique[key] = id;
                    indices.push_back(quint32(id));
                } else
                    indices.push_back(quint32(it->second));
            }
            QCOMPARE(indices.size() % 3, 0);
            std::map<std::pair<int, int>, int> edges;
            for (qsizetype i = 0; i < indices.size(); i += 3) {
                const quint32 t[3] = {indices[i], indices[i + 1], indices[i + 2]};
                QVERIFY2(t[0] != t[1] && t[1] != t[2] && t[0] != t[2],
                         qPrintable(QStringLiteral("degenerate triangle in ") + shapeKey(shape)));
                for (int k = 0; k < 3; ++k) {
                    int a = int(t[k]), b = int(t[(k + 1) % 3]);
                    if (a > b)
                        std::swap(a, b);
                    ++edges[{a, b}];
                }
            }
            int open = 0;
            for (const auto &edge : edges)
                if (edge.second != 2)
                    ++open;
            QVERIFY2(open == 0, qPrintable(QStringLiteral("%1 non-manifold edges in %2")
                                               .arg(open).arg(shapeKey(shape))));
        }
    }
    void nativeHscContactCcdAndReplay() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        Scene scene;scene.reset(false);Object soft;soft.id="soft";soft.name="FEM";soft.position={0,.25f,0};soft.dimensions={1.6f,.5f,1.6f};soft.mass=50;soft.deformable=true;soft.femResolution=2;soft.pinBottom=true;soft.youngModulus=20000;
        Object body;body.id="body";body.name="ABD";body.position={.12f,.73f,.08f};body.dimensions={.4f,.4f,.4f};body.mass=2;body.linearVelocity={.3f,-1,0};scene.objects={soft,body};
        HscOptions options;options.enabled=true;Physics physics;QString error;QVERIFY2(physics.start(scene,&error,options),qPrintable(error));
        Recording recording;recording.begin(physics.preview,{{"producer","native HSC"}});int peakContacts=0,peakRank=0;
        for(int i=0;i<20;++i){QVERIFY2(physics.step(1,&error),qPrintable(error));peakContacts=std::max(peakContacts,physics.contacts());peakRank=std::max(peakRank,physics.hscStats().rank);QVERIFY(physics.hscStats().converged);QVERIFY2(recording.append(physics.time(),physics.preview,&error),qPrintable(error));}
        QVERIFY2(peakContacts>0,"IPC contact stencils must actually be active");QVERIFY2(peakRank>0,"FEM/ABD contact must create a nonzero HSC coupling block");
        QTemporaryDir temp(QDir::currentPath()+"/native-recording-XXXXXX");QVERIFY(recording.save(temp.filePath("native.bgrec"),&error));Recording restored;QVERIFY2(restored.load(temp.filePath("native.bgrec"),&error),qPrintable(error));QVector<Object> frame;QVERIFY(restored.frame(restored.frameCount()-1,frame,&error));QCOMPARE(frame.size(),physics.preview.objects.size());
        for(int i=0;i<frame.size();++i)QCOMPARE(frame[i].mesh->positions,physics.preview.objects[i].mesh->positions);
        // A 100 m/s impact crosses the slab in one unconstrained step: CCD must prevent tunnelling.
        // A plane's visible face is its local XZ plane, so its top face lies at the object's y, and
        // Object defaults to y = 0.5; without this the body would start underneath the floor.
        physics.reset();scene.reset(false);Object floor;floor.id="floor";floor.name="floor";floor.shape=Shape::Plane;floor.position={0,0,0};floor.dimensions={4,1,4};floor.fixed=true;
        body.position={0,.3f,0};body.dimensions={.1f,.1f,.1f};body.linearVelocity={0,-100,0};scene.objects={floor,body};
        QVERIFY2(physics.start(scene,&error,options),qPrintable(error));QVERIFY2(physics.step(1,&error),qPrintable(error));auto object=physics.preview.find("body");QVERIFY(object);float minimum=1000;for(auto p:object->mesh->positions)minimum=std::min(minimum,object->matrix().map(p).y());QVERIFY2(minimum>=-1e-5f,qPrintable(QString::number(minimum)));QVERIFY(physics.contacts()>0);
#endif
    }
    void nativeHscJoints() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        for(QString type:{"spherical","revolute","prismatic","fixed","rotation"}) {
            Scene scene;scene.reset(false);Object a;a.id="a";a.name="anchor";a.fixed=true;a.collisionEnabled=false;a.dimensions={.2f,.2f,.2f};Object b=a;b.id="b";b.name="body";b.fixed=false;b.position={.5f,0,0};scene.objects={a,b};
            Link link;link.id="link";QVERIFY(Link::typeFromKey(type,link.type));link.a="a";link.b="b";
            link.anchorA={0,0,0};link.anchorB={-.5f,0,0};link.axis={0,0,1};link.strength=1e4;
            link.angularVelocity=30*3.141592653589793/180.0;
            scene.links={link};
            Physics physics;HscOptions options;options.enabled=true;QString error;QVERIFY2(physics.start(scene,&error,options),qPrintable(type+": "+error));QVERIFY2(physics.step(3,&error),qPrintable(type+": "+error));QVERIFY(physics.hscStats().converged);
            QCOMPARE(physics.linkStats().size(),1);QCOMPARE(physics.linkStats().first().type,type);
        }
#endif
    }
    void tetrahedralAssetImport() {
        QTemporaryDir temp(QDir::currentPath()+"/tet-import-XXXXXX");QFile file(temp.filePath("volume.msh"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("$MeshFormat\n2.2 0 8\n$EndMeshFormat\n$Nodes\n4\n10 0 0 0\n20 1 0 0\n30 0 1 0\n40 0 0 1\n$EndNodes\n$Elements\n1\n5 4 0 10 20 30 40\n$EndElements\n");file.close();
        QVector<Object> objects;QString error;QVERIFY2(Assets::importFile(file.fileName(),objects,&error),qPrintable(error));QCOMPARE(objects.size(),1);QCOMPARE(objects[0].mesh->tetrahedra.size(),4);QCOMPARE(objects[0].mesh->indices.size(),12);QVERIFY(objects[0].deformable);
        Scene scene;scene.reset(false);scene.objects=objects;Scene restored;QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));QCOMPARE(restored.objects[0].mesh->tetrahedra,objects[0].mesh->tetrahedra);
        if(Physics::available()){Physics physics;QVERIFY2(physics.start(restored,&error),qPrintable(error));QVERIFY2(physics.step(2,&error),qPrintable(error));}
#ifdef BAGUA_TEST_CUDA
        // The same asset must also drive the native FEM backend, not only the Chrono baseline.
        if(HscNative::available()){Physics native;HscOptions options;options.enabled=true;QVERIFY2(native.start(restored,&error,options),qPrintable(error));QVERIFY2(native.step(2,&error),qPrintable(error));QVERIFY(native.hscActive());}
#endif
    }
    void linkModelPersistenceAndValidation() {
        Scene scene;scene.reset(false);
        Object base;base.id="base";base.name="base";base.fixed=true;base.position={0,.8f,0};base.dimensions={.4f,.4f,.4f};base.anchors={{0,-.2f,0}};
        Object slider;slider.id="slider";slider.name="slider";slider.position={0,.5f,0};slider.dimensions={.1f,.1f,.1f};slider.anchors={{0,0,0}};
        Object wheel;wheel.id="wheel";wheel.name="wheel";wheel.position={0,.299f,0};wheel.dimensions={.3f,.3f,.3f};wheel.anchors={{0,.15f,0}};
        scene.objects={base,slider,wheel};scene.timeStep=1.0/120.0;
        Link prismatic;prismatic.id="prismatic";prismatic.type=Link::Type::Prismatic;prismatic.a="base";prismatic.b="slider";
        prismatic.anchorA={0,-.2f,0};prismatic.anchorB={0,.05f,0};prismatic.axis={0,1,0};prismatic.strength=1e5;
        prismatic.hasLimit=true;prismatic.lower=-.15;prismatic.upper=.15;prismatic.limitStrength=100;
        Link spring;spring.id="spring";spring.type=Link::Type::Stitch;spring.a="base";spring.b="slider";
        spring.pairs={{0,0}};spring.anchorPairs=true;spring.stiffness=20000;spring.rest=.1;
        Link revolute;revolute.id="revolute";revolute.type=Link::Type::Revolute;revolute.a="slider";revolute.b="wheel";
        revolute.anchorA={0,-.05f,0};revolute.anchorB={0,.15f,0};revolute.axis={0,1,0};revolute.strength=1e5;
        Link fixed;fixed.id="fixed";fixed.type=Link::Type::Fixed;fixed.a="base";fixed.b="wheel";fixed.anchorA={0,0,0};fixed.anchorB={0,0,0};fixed.strength=100;
        Link spherical;spherical.id="spherical";spherical.type=Link::Type::Spherical;spherical.a="base";spherical.b="wheel";
        spherical.anchorA={0,0,0};spherical.anchorB={0,0,0};spherical.strength=100;
        scene.links={prismatic,spring,revolute,fixed,spherical};
        scene.exclusions={{"slider","wheel"}};
        // Round trip: every field of every link type survives save/load.
        Scene restored;QString error;QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));
        QCOMPARE(restored.links.size(),5);QCOMPARE(restored.exclusions.size(),1);
        QCOMPARE(restored.objects[0].anchors.size(),1);
        QCOMPARE(restored.timeStep,scene.timeStep);
        const Link &back=restored.links[0];
        QCOMPARE(back.typeKey(),QStringLiteral("prismatic"));QVERIFY(back.hasLimit);
        QCOMPARE(back.lower,-.15);QCOMPARE(back.upper,.15);QCOMPARE(back.strength,1e5);
        QCOMPARE(back.anchorA,QVector3D(0,-.2f,0));QCOMPARE(back.axis,QVector3D(0,1,0));
        QVERIFY(restored.links[1].anchorPairs);QCOMPARE(restored.links[1].stiffness,20000.0);QCOMPARE(restored.links[1].rest,.1);
        QCOMPARE(restored.links[1].pairs.size(),1);
        QCOMPARE(restored.json(),scene.json());
        // Invalid input must be refused and must not replace the current scene.
        auto broken=[&](const std::function<void(QJsonObject &)> &edit) {
            auto json=scene.json();edit(json);Scene target;QString message;
            QVERIFY2(!target.loadJson(json,&message),qPrintable(message));
        };
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[0].toObject();e["type"]="nonsense";l[0]=e;j["links"]=l;});
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[0].toObject();e["b"]="absent";l[0]=e;j["links"]=l;});
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[0].toObject();e["a"]="base";e["b"]="base";l[0]=e;j["links"]=l;});
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[0].toObject();e["limit"]=QJsonObject{{"lower",1.0},{"upper",-1.0}};l[0]=e;j["links"]=l;});
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[0].toObject();e["axis"]=QJsonArray{0,0,0};l[0]=e;j["links"]=l;});
        broken([](QJsonObject &j){j["dt"]=0.5;});
        broken([](QJsonObject &j){auto e=j["objects"].toArray()[0].toObject();e["fem"]=QJsonObject{{"youngModulus",5000.0},{"poissonRatio",.3},{"resolution",2}};auto o=j["objects"].toArray();o[0]=e;j["objects"]=o;});
        broken([](QJsonObject &j){auto l=j["links"].toArray();auto e=l[1].toObject();e["pairs"]=QJsonArray{QJsonArray{5,0}};l[1]=e;j["links"]=l;});
        // Version-1 joints (world anchors, legacy names) still load and become body-local links.
        QJsonObject legacy{{"format","baguadt.scene"},{"version",1},{"units","m"},{"upAxis","Y"},
                           {"objects",QJsonArray{QJsonObject{{"id","a"},{"name","a"},{"shape","box"},{"position",QJsonArray{0,2,0}},
                                                            {"rotation",QJsonArray{0,0,0}},{"scale",QJsonArray{1,1,1}},{"dimensions",QJsonArray{1,1,1}},
                                                            {"color","#ffffff"},{"visible",true},{"fixed",true}},
                                                QJsonObject{{"id","b"},{"name","b"},{"shape","box"},{"position",QJsonArray{0,1,0}},
                                                            {"rotation",QJsonArray{0,0,0}},{"scale",QJsonArray{1,1,1}},{"dimensions",QJsonArray{1,1,1}},
                                                            {"color","#ffffff"},{"visible",true},{"fixed",false}}}},
                           {"joints",QJsonArray{QJsonObject{{"id","legacy"},{"type","hinge"},{"bodyA","a"},{"bodyB","b"},
                                                            {"anchor",QJsonArray{0,1.5,0}},{"axis",QJsonArray{0,0,1}},{"strength",100}}}}};
        Scene migrated;QVERIFY2(migrated.loadJson(legacy,&error),qPrintable(error));
        QCOMPARE(migrated.links.size(),1);QCOMPARE(migrated.links[0].typeKey(),QStringLiteral("revolute"));
        // World anchor (0,1.5,0) relative to body a at y=2 is 0.5 below its centre.
        QCOMPARE(migrated.links[0].anchorA,QVector3D(0,-.5f,0));
        QCOMPARE(migrated.links[0].anchorB,QVector3D(0,.5f,0));
    }
    void nativeArticulatedSuspension() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        // A miniature suspension: fixed base -> prismatic guide with limits -> revolute wheel,
        // with the base and slider joined by a rest-length spring stitch.
        Scene scene;scene.reset(false);
        Object base;base.id="base";base.name="base";base.position={0,.8f,0};base.dimensions={.4f,.4f,.4f};base.fixed=true;
        base.anchors={{0,-.2f,0},{0,0,0}};
        Object slider;slider.id="slider";slider.name="slider";slider.position={0,.5f,0};slider.dimensions={.1f,.1f,.1f};
        slider.mass=1;slider.anchors={{0,0,0},{0,-.05f,0}};
        Object wheel;wheel.id="wheel";wheel.name="wheel";wheel.position={0,.299f,0};wheel.dimensions={.3f,.3f,.3f};
        wheel.mass=2;wheel.anchors={{0,.15f,0}};
        scene.objects={base,slider,wheel};
        Link prismatic;prismatic.id="prismatic";prismatic.type=Link::Type::Prismatic;prismatic.a="base";prismatic.b="slider";
        prismatic.anchorA={0,-.2f,0};prismatic.anchorB={0,.05f,0};prismatic.axis={0,1,0};prismatic.strength=1e5;
        prismatic.hasLimit=true;prismatic.lower=-.15;prismatic.upper=.15;prismatic.limitStrength=100;
        Link spring;spring.id="spring";spring.type=Link::Type::Stitch;spring.a="base";spring.b="slider";
        spring.pairs={{1,0}};spring.anchorPairs=true;spring.stiffness=20000;spring.rest=.3;
        Link revolute;revolute.id="revolute";revolute.type=Link::Type::Revolute;revolute.a="slider";revolute.b="wheel";
        revolute.anchorA={0,-.05f,0};revolute.anchorB={0,.15f,0};revolute.axis={0,1,0};revolute.strength=1e5;
        scene.links={prismatic,spring,revolute};
        scene.timeStep=1.0/120.0;
        Scene restored;QString error;QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));
        HscOptions options;options.enabled=true;Physics physics;
        QVERIFY2(physics.start(restored,&error,options),qPrintable(error));
        QCOMPARE(physics.stepSize(),1.0/120.0);
        for(int i=0;i<240;++i)QVERIFY2(physics.step(1,&error),qPrintable(error));
        QVERIFY(physics.hscStats().converged);
        const auto stats=physics.linkStats();
        QCOMPARE(int(stats.size()),3);
        auto stat=[&](const QString &id) {
            for(const auto &entry:stats)if(entry.id==id)return entry;
            return LinkStat{};
        };
        const auto guide=stat("prismatic"),coil=stat("spring"),hinge=stat("revolute");
        // The guide keeps the slider on the axis and inside its travel limit; |distance| is the
        // authored 5 cm gap between the guide anchors.
        QVERIFY(guide.hasDistance);QVERIFY(guide.hasLimit);
        QVERIFY2(guide.distance>=guide.limitLower && guide.distance<=guide.limitUpper,qPrintable(QString::number(guide.distance)));
        QVERIFY2(std::abs(std::abs(guide.distance)-0.05)<0.01,qPrintable(QString::number(guide.distance)));
        // The spring carries the slider+wheel weight: 3*9.81/20000 = 1.5 mm of extra length.
        QVERIFY2(coil.error<0.005,qPrintable(QString::number(coil.error)));
        // The wheel is held by the hinge instead of falling (a free wheel would be far below).
        auto object=physics.preview.find("wheel");QVERIFY(object && object->mesh);
        // Average only the shape vertices: the visual mesh ends with the virtual anchors.
        const qsizetype shapeVertices=object->mesh->positions.size()-wheel.anchors.size();
        QVERIFY(shapeVertices>=8);
        float center=0;for(qsizetype i=0;i<shapeVertices;++i)center+=object->matrix().map(object->mesh->positions[i]).y();center/=float(shapeVertices);
        QVERIFY2(std::abs(center-0.299f)<0.01f,qPrintable(QString::number(center)));
        QVERIFY(hinge.hasAngle);
        qInfo("articulated suspension: guide=%.6f limit=[%.3f,%.3f] springError=%.6f hingeAngle=%.6f wheelY=%.6f",guide.distance,guide.limitLower,guide.limitUpper,coil.error,hinge.angle,double(center));
#endif
    }
    void nativeContactExclusionAllowsOverlap() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        // Vehicle proxies overlap on purpose (guide in its slider, hub inside the rim); IPC
        // cannot start from intersecting surfaces, so the excluded pair must be accepted and
        // must not push the bodies apart.
        Scene scene;scene.reset(false);
        Object base;base.id="base";base.name="base";base.position={0,0,0};base.dimensions={.4f,.4f,.4f};base.fixed=true;
        Object body;body.id="body";body.name="body";body.position={.3f,0,0};body.dimensions={.4f,.4f,.4f};body.mass=2;
        scene.objects={base,body};scene.exclusions={{"base","body"}};
        HscOptions options;options.enabled=true;Physics physics;QString error;
        QVERIFY2(physics.start(scene,&error,options),qPrintable(error));
        QVERIFY2(physics.step(60,&error),qPrintable(error));
        QVERIFY(physics.hscStats().converged);
        auto object=physics.preview.find("body");QVERIFY(object && object->mesh);
        float center=0;for(auto p:object->mesh->positions)center+=object->matrix().map(p).x();center/=float(object->mesh->positions.size());
        QVERIFY2(std::abs(center-0.3f)<0.005f,qPrintable(QString::number(center)));
        QCOMPARE(physics.contacts(),0);
#endif
    }
    void linkSceneGuiFlow() {
        // Mirrors what a user does with an assembly scene: open, play with the timer loop,
        // reset, play again, then close the window.
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        if (!Physics::available() || !HscNative::available()) {
            QVERIFY(w.close());
            return;
        }
        Scene source;source.reset(false);
        Object base;base.id="base";base.name="base";base.fixed=true;base.position={0,.6f,0};base.dimensions={.4f,.4f,.4f};base.anchors={{0,-.2f,0}};
        Object body;body.id="body";body.name="body";body.position={0,.3f,0};body.dimensions={.1f,.1f,.1f};body.anchors={{0,0,0}};
        source.objects={base,body};
        Link guide;guide.id="guide";guide.type=Link::Type::Prismatic;guide.a="base";guide.b="body";
        guide.anchorA={0,-.2f,0};guide.anchorB={0,0,0};guide.axis={0,1,0};guide.strength=1e5;
        guide.hasLimit=true;guide.lower=-.1;guide.upper=.1;
        Link spring;spring.id="spring";spring.type=Link::Type::Stitch;spring.a="base";spring.b="body";
        spring.pairs={{0,0}};spring.anchorPairs=true;spring.stiffness=20000;spring.rest=.3;
        source.links={guide,spring};
        w.newScene(false);
        QString error;
        QVERIFY2(w.scene->loadJson(source.json(),&error),qPrintable(error));
        w.scene->undo.setClean();
        HscOptions nativeOptions;nativeOptions.enabled=true;
        w.setPhysicsOptions(nativeOptions);
        // First run: the timer loop must advance the native world and stay valid.
        QVERIFY(w.startSimulation());
        QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime()>0.02,5000);
        QVERIFY(w.physicsOptions().enabled);
        // Reset tears the native world down; a second run rebuilds it.
        w.resetSimulation();
        QVERIFY(!w.simulationActive());
        QVERIFY(w.startSimulation());
        QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime()>0.02,5000);
        w.pauseSimulation();
        w.resetSimulation();
        QVERIFY(w.close());
    }
    void linkSceneCloseWhileRunning() {
        // Closing the window while the timer loop drives the native world: teardown order must
        // stop the loop and release the native world before the DLL is unloaded.
        auto build=[&](MainWindow &w) {
            Scene source;source.reset(false);
            Object base;base.id="base";base.name="base";base.fixed=true;base.position={0,.6f,0};base.dimensions={.4f,.4f,.4f};base.anchors={{0,-.2f,0}};
            Object body;body.id="body";body.name="body";body.position={0,.3f,0};body.dimensions={.1f,.1f,.1f};body.anchors={{0,0,0}};
            source.objects={base,body};
            Link spring;spring.id="spring";spring.type=Link::Type::Stitch;spring.a="base";spring.b="body";
            spring.pairs={{0,0}};spring.anchorPairs=true;spring.stiffness=20000;spring.rest=.3;
            source.links={spring};
            w.newScene(false);QString error;QVERIFY2(w.scene->loadJson(source.json(),&error),qPrintable(error));
            w.scene->undo.setClean();
            HscOptions nativeOptions;nativeOptions.enabled=true;w.setPhysicsOptions(nativeOptions);
        };
        {
            MainWindow w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
            if (!Physics::available() || !HscNative::available()) {QVERIFY(w.close());return;}
            build(w);
            QVERIFY(w.startSimulation());
            QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime()>0.02,5000);
            QVERIFY(w.close());   // still running, never paused
        }
        {
            MainWindow w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));
            build(w);
            QVERIFY(w.startSimulation());
            QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime()>0.02,5000);
            w.newScene(false);    // switch away from the running document
            QVERIFY(w.startSimulation());
            QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime()>0.02,5000);
            QVERIFY(w.close());
        }
    }
    void assemblyPanelEditsLinks() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newScene(false);
        Object base;base.id="base";base.name="base";base.position={0,.6f,0};base.dimensions={.4f,.4f,.4f};
        Object body;body.id="body";body.name="body";body.position={0,.3f,0};body.dimensions={.1f,.1f,.1f};
        w.scene->objects={base,body};
        w.scene->notify();
        w.scene->undo.setClean();
        auto *type = w.findChild<QComboBox *>("linkType");
        auto *a = w.findChild<QComboBox *>("linkBodyA");
        auto *b = w.findChild<QComboBox *>("linkBodyB");
        auto *add = w.findChild<QPushButton *>("linkAdd");
        auto *update = w.findChild<QPushButton *>("linkUpdate");
        auto *remove = w.findChild<QPushButton *>("linkRemove");
        auto *tree = w.findChild<QTreeWidget *>("linkTree");
        auto *anchorAdd = w.findChild<QPushButton *>("anchorAdd");
        auto *anchorList = w.findChild<QListWidget *>("anchorList");
        auto *exclusionAdd = w.findChild<QPushButton *>("exclusionAdd");
        auto *exclusionRemove = w.findChild<QPushButton *>("exclusionRemove");
        auto *exclusionList = w.findChild<QListWidget *>("exclusionList");
        auto *exclusionA = w.findChild<QComboBox *>("exclusionBodyA");
        auto *exclusionB = w.findChild<QComboBox *>("exclusionBodyB");
        QVERIFY(type && a && b && add && update && remove && tree && anchorAdd && anchorList);
        QVERIFY(exclusionAdd && exclusionRemove && exclusionList && exclusionA && exclusionB);
        // The panel lists both objects for A/B selection.
        QCOMPARE(a->count(),2);QCOMPARE(b->count(),2);
        // Anchors: adding one through the panel writes the object's anchor list.
        w.scene->select("base");
        anchorAdd->click();
        QCOMPARE(w.scene->find("base")->anchors.size(),1);
        QCOMPARE(anchorList->count(),1);
        w.scene->undo.undo();
        QCOMPARE(w.scene->find("base")->anchors.size(),0);
        w.scene->undo.redo();
        // Create a prismatic link with limit through the form.
        type->setCurrentIndex(type->findData("prismatic"));
        a->setCurrentIndex(a->findData("base"));
        b->setCurrentIndex(b->findData("body"));
        w.findChild<QDoubleSpinBox *>("linkAnchorAX")->setValue(0);
        w.findChild<QDoubleSpinBox *>("linkAnchorAY")->setValue(-0.2);
        w.findChild<QDoubleSpinBox *>("linkAnchorBZ")->setValue(0);
        w.findChild<QDoubleSpinBox *>("linkAxisY")->setValue(1);
        w.findChild<QDoubleSpinBox *>("linkStrength")->setValue(2500);
        auto *limitEnabled = w.findChild<QCheckBox *>("limitEnabled");
        QVERIFY(limitEnabled);
        limitEnabled->setChecked(true);
        w.findChild<QDoubleSpinBox *>("limitLower")->setValue(-0.1);
        w.findChild<QDoubleSpinBox *>("limitUpper")->setValue(0.1);
        add->click();
        QCOMPARE(w.scene->links.size(),1);
        QCOMPARE(w.scene->links[0].typeKey(),QStringLiteral("prismatic"));
        QCOMPARE(w.scene->links[0].a,QStringLiteral("base"));QCOMPARE(w.scene->links[0].b,QStringLiteral("body"));
        QCOMPARE(w.scene->links[0].strength,2500.0);QVERIFY(w.scene->links[0].hasLimit);
        QCOMPARE(w.scene->links[0].lower,-.1);QCOMPARE(w.scene->links[0].anchorA,QVector3D(0,-.2f,0));
        QCOMPARE(tree->topLevelItemCount(),1);
        // Undo removes it, redo restores it.
        w.scene->undo.undo();
        QCOMPARE(w.scene->links.size(),0);
        w.scene->undo.redo();
        QCOMPARE(w.scene->links.size(),1);
        // Rejecting an invalid form keeps the link list untouched.
        const int undoIndex = w.scene->undo.index();
        b->setCurrentIndex(b->findData("base"));   // same body on both sides
        add->click();
        QCOMPARE(w.scene->links.size(),1);
        QCOMPARE(w.scene->undo.index(),undoIndex);
        b->setCurrentIndex(b->findData("body"));
        // Update the selected link's strength.
        tree->setCurrentItem(tree->topLevelItem(0));
        w.findChild<QDoubleSpinBox *>("linkStrength")->setValue(8000);
        update->click();
        QCOMPARE(w.scene->links.size(),1);
        QCOMPARE(w.scene->links[0].strength,8000.0);
        // Contact exclusion add/remove.
        exclusionA->setCurrentIndex(exclusionA->findData("base"));
        exclusionB->setCurrentIndex(exclusionB->findData("body"));
        exclusionAdd->click();
        QCOMPARE(w.scene->exclusions.size(),1);
        QCOMPARE(exclusionList->count(),1);
        exclusionList->setCurrentRow(0);
        exclusionRemove->click();
        QCOMPARE(w.scene->exclusions.size(),0);
        // Delete the link through the panel.
        tree->setCurrentItem(tree->topLevelItem(0));
        remove->click();
        QCOMPARE(w.scene->links.size(),0);
        w.scene->undo.setClean();
        QVERIFY(w.close());
    }
    void assemblyReferentialIntegrity() {
        Scene scene;scene.reset(false);Object a;a.id="a";a.name="a";a.anchors={{0,0,0},{0,1,0},{0,2,0}};Object b=a;b.id="b";b.name="b";
        scene.objects={a,b};Link stitch;stitch.id="spring";stitch.type=Link::Type::Stitch;stitch.a="a";stitch.b="b";stitch.anchorPairs=true;stitch.pairs={{0,0},{1,1},{2,2}};scene.links={stitch};scene.exclusions={{"a","b"}};
        const auto original=scene.json();scene.removeAnchor("a",1);QCOMPARE(scene.links[0].pairs,(QVector<QPair<int,int>>{{0,0},{1,2}}));Scene restored;QString error;QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));
        scene.undo.undo();QCOMPARE(scene.json(),original);scene.undo.redo();QCOMPARE(scene.find("a")->anchors.size(),2);scene.undo.undo();
        scene.select("a");scene.removeSelected();QVERIFY(scene.links.isEmpty());QVERIFY(scene.exclusions.isEmpty());QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));scene.undo.undo();QCOMPARE(scene.json().value("links"),original["links"]);QCOMPARE(scene.json().value("exclusions"),original["exclusions"]);
        auto broken=original;auto ls=broken["links"].toArray();auto l=ls[0].toObject();l["anchorPairs"]=false;l["pairs"]=QJsonArray{QJsonArray{0,99999}};ls[0]=l;broken["links"]=ls;QVERIFY(!restored.loadJson(broken,&error));
    }
    void assemblyPanelSelectionAndPicking() {
        MainWindow w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));w.newScene(false);Object a;a.id="a";a.name="a";a.position={0,1,0};a.dimensions={2,1,1};a.scale={2,1,1};Object b=a;b.id="b";b.name="b";b.position={3,1,0};w.scene->objects={a,b};w.scene->notify();
        auto *type=w.findChild<QComboBox *>("linkType"),*left=w.findChild<QComboBox *>("linkBodyA"),*right=w.findChild<QComboBox *>("linkBodyB");left->setCurrentIndex(left->findData("a"));right->setCurrentIndex(right->findData("b"));
        auto *add=w.findChild<QPushButton *>("linkAdd");add->click();add->click();QCOMPARE(w.scene->links.size(),2);QVERIFY(w.scene->links[0].id!=w.scene->links[1].id);Scene restored;QString error;QVERIFY2(restored.loadJson(w.scene->json(),&error),qPrintable(error));
        auto *limit=w.findChild<QCheckBox *>("limitEnabled");limit->setChecked(true);QVERIFY(w.findChild<QDoubleSpinBox *>("limitLower")->isEnabled());limit->setChecked(false);QVERIFY(!w.findChild<QDoubleSpinBox *>("limitLower")->isEnabled());
        auto *v=w.findChild<Viewport *>();QVERIFY(v);w.scene->select("a");auto *pick=w.findChild<QPushButton *>("anchorPick");pick->click();QVERIFY(v->anchorPicking);auto world=a.anchorMatrix().map({.3f,0,0});QVERIFY(QMetaObject::invokeMethod(v,"clicked",Qt::DirectConnection,Q_ARG(QVector3D,world),Q_ARG(QString,QString("a"))));QCOMPARE(w.scene->find("a")->anchors.size(),1);QVERIFY((w.scene->find("a")->anchors[0]-QVector3D(.3f,0,0)).length()<1e-5f);QVERIFY(!v->anchorPicking);
        w.scene->undo.setClean();QVERIFY(w.close());
    }
    void sceneTimeStepForChrono() {
        if(!Physics::available())QSKIP("Chrono disabled");Scene scene;scene.reset(false);Object o;o.id="a";o.name="a";o.position={0,10,0};o.collisionEnabled=false;scene.objects={o};scene.timeStep=1./120.;Physics physics;QString error;QVERIFY2(physics.start(scene,&error),qPrintable(error));QCOMPARE(physics.stepSize(),scene.timeStep);QVERIFY(physics.step(2,&error));QVERIFY(std::abs(physics.time()-1./60.)<1e-12);
        scene.exclusions={{"a","missing"}};QVERIFY(!physics.start(scene,&error));
    }
    void nativeScaledAnchorsAndPlaybackFrames() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        Scene scene;scene.reset(false);Object a;a.id="a";a.name="a";a.position={0,2,0};a.rotation={13,24,7};a.dimensions={.2f,.4f,.6f};a.scale={2,3,4};a.anchors={{.03f,.05f,.07f}};a.linearVelocity={1,0,0};a.collisionEnabled=false;scene.objects={a};Physics physics;HscOptions options;options.enabled=true;QString error;QVERIFY2(physics.start(scene,&error,options),qPrintable(error));
        auto *body=physics.preview.find("a");auto original=a.anchorMatrix().map(a.anchors[0]);QVERIFY((body->anchorMatrix().map(a.anchors[0])-original).length()<1e-5f);QVERIFY2(physics.step(4,&error),qPrintable(error));body=physics.preview.find("a");auto moving=body->anchorMatrix().map(a.anchors[0]);QVERIFY(moving.x()>original.x()+.015f);auto actual=body->matrix().map(body->mesh->positions.last());QVERIFY((actual-moving).length()<1e-5f);
        Recording recording;recording.begin(physics.preview);QVERIFY(physics.step(1,&error));QVERIFY(recording.append(physics.time(),physics.preview,&error));QTemporaryDir temp(QDir::currentPath()+"/frames-XXXXXX");QVERIFY(recording.save(temp.filePath("frames.brec"),&error));Recording loaded;QVERIFY2(loaded.load(temp.filePath("frames.brec"),&error),qPrintable(error));QVector<Object> frame;QVERIFY(loaded.frame(1,frame,&error));QVERIFY((frame[0].anchorMatrix().map(a.anchors[0])-physics.preview.find("a")->anchorMatrix().map(a.anchors[0])).length()<1e-5f);
#endif
    }
    void nativeSolverFailureReturnsError() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("Native CUDA backend disabled");
#else
        Scene scene;scene.reset(false);Object soft;soft.id="soft";soft.name="soft";soft.deformable=true;soft.pinBottom=true;soft.femResolution=2;soft.position={0,1,0};scene.objects={soft};HscOptions options;options.enabled=true;options.maxIterations=1;options.linearTolerance=1e-12;Physics physics;QString error;QVERIFY2(physics.start(scene,&error,options),qPrintable(error));QVERIFY(!physics.step(1,&error));QVERIFY(!error.isEmpty());QVERIFY(!physics.step(1,&error));physics.reset();options.maxIterations=2000;options.linearTolerance=1e-4;QVERIFY2(physics.start(scene,&error,options),qPrintable(error));QVERIFY2(physics.step(2,&error),qPrintable(error));
#endif
    }
    void reopeningProjectPreservesExistingTakes() {
        QTemporaryDir temp(QDir::currentPath()+"/project-audit-XXXXXX");Scene source;source.reset(false);source.create(Shape::Box);QString error;QVERIFY(source.save(temp.filePath("scene.bagua.json"),&error));Recording take;take.begin(source);source.objects[0].position.setX(2);QVERIFY(take.append(.1,source,&error));QVERIFY(take.save(temp.filePath("take.brec"),&error));QFile file(temp.filePath("project.bagua.project.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(QJsonObject{{"format","baguadt.project"},{"version",1},{"scenes",QJsonArray{QJsonObject{{"scene","scene.bagua.json"},{"recordings",QJsonArray{"take.brec"}},{"mode","replay"}}}}}).toJson());file.close();
        MainWindow w;w.show();QVERIFY(QTest::qWaitForWindowExposed(&w));QVERIFY(w.openPath(temp.filePath("scene.bagua.json")));auto *original=w.scene;original->create(Shape::Sphere);const auto authored=original->json();auto *tabs=w.findChild<QTabBar *>("sceneTabs");int count=tabs->count();QVERIFY(w.openProject(file.fileName()));QCOMPARE(tabs->count(),count+1);QCOMPARE(original->json().value("objects"),authored["objects"]);QCOMPARE(original->json().value("links"),authored["links"]);QVERIFY(w.replayActive());QCOMPARE(w.recordedFrames(),2);w.stopReplay();QVERIFY(w.openProject(file.fileName()));QCOMPARE(tabs->count(),count+2);QCOMPARE(original->json().value("objects"),authored["objects"]);QCOMPARE(original->json().value("links"),authored["links"]);original->undo.setClean();QVERIFY(w.close());
    }
    void paperRotationDerivativesAndFrames() {
        std::array<double,12> a{},b{};a[3]=a[7]=a[11]=b[3]=b[7]=b[11]=1;
        double axis[3]={0,0,1},alignment[9]={1,0,0,0,1,0,0,0,1},r[9];
        paperRodrigues(a.data()+3,axis,.4,r);
        for(int i=0;i<9;++i)b[size_t(i)+3]=r[i];
        QVERIFY(std::abs(paperQuadraticRotation(a.data(),b.data(),r,alignment,10))<1e-24);
        // A common world rotation must not change relative-frame constraint feasibility.
        auto q=QQuaternion::fromAxisAndAngle(QVector3D(0,1,0),35).toRotationMatrix();
        auto old=b;
        for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
            a[size_t(3+i*3+j)]=q(i,j);b[size_t(3+i*3+j)]=0;
            for(int k=0;k<3;++k)b[size_t(3+i*3+j)]+=q(i,k)*old[size_t(3+k*3+j)];
        }
        paperRodrigues(a.data()+3,axis,.4,r);
        QVERIFY(paperQuadraticRotation(a.data(),b.data(),r,alignment,10)<1e-12);
        a[4]+=.07;b[7]-=.03;paperRodrigues(a.data()+3,axis,.4,r);
        double g[24],h[576];paperQuadraticRotation(a.data(),b.data(),r,alignment,10,g,h);
        // Lagged R stays fixed during derivative checks, matching Newton/backtracking.
        for(int i=0;i<24;++i) {
            double &x=i<12?a[size_t(i)]:b[size_t(i-12)],before=x,step=1e-5;
            x=before+step;double gp[24];double ep=paperQuadraticRotation(a.data(),b.data(),r,alignment,10,gp);
            x=before-step;double gm[24];double em=paperQuadraticRotation(a.data(),b.data(),r,alignment,10,gm);x=before;
            QVERIFY(std::abs((ep-em)/(2*step)-g[i])<1e-6);
            for(int j=0;j<24;++j){QVERIFY(std::abs((gp[j]-gm[j])/(2*step)-h[j*24+i])<1e-6);QVERIFY(std::abs(h[i*24+j]-h[j*24+i])<1e-12);}
        }
        double curvature=0;for(int i=0;i<24;++i)for(int j=0;j<24;++j)curvature+=std::sin(i+1.)*h[i*24+j]*std::sin(j+1.);
        QVERIFY(curvature>=-1e-10);
    }
    void jointsPersistenceAndChronoMapping() {
        Scene scene;scene.reset(false);Object a,b;a.id="a";a.name="parent";a.fixed=true;a.position={0,2,0};b=a;b.id="b";b.name="child";b.fixed=false;b.position={0,1,0};scene.objects={a,b};
        for(auto type:{"spherical","revolute","prismatic","fixed","rotation"}) {
            Link link;link.id="link";QVERIFY(Link::typeFromKey(type,link.type));link.a="a";link.b="b";
            // Anchors coincide in world space halfway between the two bodies.
            link.anchorA={0,-.5f,0};link.anchorB={0,.5f,0};link.axis={0,0,1};link.strength=100;
            scene.links={link};
            Scene restored;QString error;QVERIFY2(restored.loadJson(scene.json(),&error),qPrintable(error));QCOMPARE(restored.links.size(),1);
            QCOMPARE(restored.links[0].typeKey(),QString(type));QCOMPARE(restored.links[0].axis,link.axis);
            if(Physics::available()){Physics physics;QVERIFY2(physics.start(restored,&error),qPrintable(error));QVERIFY2(physics.step(4,&error),qPrintable(error));}
            auto broken=scene.json();auto links=broken["links"].toArray();auto entry=links[0].toObject();entry["b"]="missing";links[0]=entry;broken["links"]=links;auto before=restored.json();QVERIFY(!restored.loadJson(broken,&error));QCOMPARE(restored.json(),before);
        }
    }
    void hscCudaSparseConformance() {
#ifndef BAGUA_TEST_CUDA
        QSKIP("CUDA disabled");
#else
        QFile file(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("data/hsc-reference.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        for (auto entry:QJsonDocument::fromJson(file.readAll()).object().value("cases").toArray()) {
            auto j=entry.toObject();HscCsr input;input.femDofs=j["femDofs"].toInt();input.rigidBlocks={12,12,12};
            auto rhs=j["rhs"].toArray(),matrix=j["matrix"].toArray();int n=rhs.size();
            input.rows.push_back(0);
            for(int i=0;i<n;++i) {
                input.rhs.push_back(rhs[i].toDouble());
                for(int k=0;k<n;++k) if(matrix[i*n+k].toDouble()!=0) {input.columns.push_back(k);input.values.push_back(matrix[i*n+k].toDouble());}
                input.rows.push_back(int(input.values.size()));
            }
            HscOptions options;HscStats stats;std::string error;std::vector<double> solution;
            QVERIFY2(solveHscCuda(input,options,solution,stats,error),error.c_str());
            QVERIFY(stats.cuda);QVERIFY(stats.rank>0);QVERIFY(stats.relativeResidual<=options.tolerance);
            QVERIFY(stats.acaResidual<1e-10);QVERIFY(stats.factorNonzeros>0);
            auto expected=j["expected"].toArray();double difference=0,norm=0;
            for(int i=0;i<n;++i){difference+=std::pow(solution[size_t(i)]-expected[i].toDouble(),2);norm+=std::pow(expected[i].toDouble(),2);}
            QVERIFY(std::sqrt(difference/norm)<1e-4);
            for (bool affineFirst:{false,true}) {
                struct Buffers {
                    HscDeviceBlock *blocks=nullptr;double *b=nullptr,*x=nullptr;
                    ~Buffers(){cudaFree(blocks);cudaFree(b);cudaFree(x);}
                } device;
                int nf=input.femDofs,nq=n-nf,fo=affineFirst?nq:0,qo=affineFirst?0:nf;
                auto canonical=[&](int i){return i>=fo && i<fo+nf?i-fo:nf+i-qo;};
                std::vector<HscDeviceBlock> packed;std::vector<double> globalB(size_t(n),0.),globalX(size_t(n),0.);
                for(int i=0;i<n;i+=3)for(int k=i;k<n;k+=3) {
                    HscDeviceBlock block{};block.row=i;block.col=k;bool nonzero=false;
                    for(int r=0;r<3;++r)for(int c=0;c<3;++c){double v=matrix[canonical(i+r)*n+canonical(k+c)].toDouble();block.value[r*3+c]=v;nonzero|=v!=0;}
                    if(nonzero)packed.push_back(block);
                }
                for(int i=0;i<n;++i)globalB[size_t(i)]=input.rhs[size_t(canonical(i))];
                QCOMPARE(cudaMalloc(&device.blocks,packed.size()*sizeof(HscDeviceBlock)),cudaSuccess);
                QCOMPARE(cudaMalloc(&device.b,size_t(n)*sizeof(double)),cudaSuccess);QCOMPARE(cudaMalloc(&device.x,size_t(n)*sizeof(double)),cudaSuccess);
                QCOMPARE(cudaMemcpy(device.blocks,packed.data(),packed.size()*sizeof(HscDeviceBlock),cudaMemcpyHostToDevice),cudaSuccess);
                QCOMPARE(cudaMemcpy(device.b,globalB.data(),size_t(n)*sizeof(double),cudaMemcpyHostToDevice),cudaSuccess);
                HscStats resident;
                QVERIFY2(solveHscDeviceBlocks(device.blocks,int(packed.size()),device.b,device.x,n,fo,nf,qo,nq,options,resident,error),error.c_str());
                QCOMPARE(cudaMemcpy(globalX.data(),device.x,size_t(n)*sizeof(double),cudaMemcpyDeviceToHost),cudaSuccess);
                for(int i=0;i<n;++i)QVERIFY(std::abs(globalX[size_t(i)]-solution[size_t(canonical(i))])<1e-6);
                QVERIFY(resident.cuda);QVERIFY(resident.relativeResidual<=options.tolerance);
            }
        }
        // Sparse FEM subsystem beyond the old 1536-DOF dense ceiling, exact x=1.
        HscCsr large;large.femDofs=1602;large.rigidBlocks={12};large.rows={0};
        for(int i=0;i<1614;++i) {
            large.columns.push_back(i);large.values.push_back(2.);double b=2;
            if(i<12 || i>=1602){large.columns.push_back(i<12?1602+i:i-1602);large.values.push_back(.1);b+=.1;}
            large.rhs.push_back(b);large.rows.push_back(int(large.values.size()));
        }
        HscOptions options;HscStats stats;std::string error;std::vector<double> solution;
        QVERIFY2(solveHscCuda(large,options,solution,stats,error),error.c_str());
        for(double x:solution) QVERIFY(std::abs(x-1)<1e-7);
        QCOMPARE(stats.nonzeros,1638);QVERIFY(stats.rank>0);
        // Pure affine and pure FEM paths remain valid and do not invent coupling.
        for(int nf:{0,12}) {
            HscCsr pure;pure.femDofs=nf;if(nf==0)pure.rigidBlocks={12};pure.rows={0};
            for(int i=0;i<12;++i){pure.columns.push_back(i);pure.values.push_back(2);pure.rhs.push_back(2);pure.rows.push_back(i+1);}
            QVERIFY2(solveHscCuda(pure,options,solution,stats,error),error.c_str());QCOMPARE(stats.rank,0);
            for(double x:solution) QVERIFY(std::abs(x-1)<1e-7);
        }
#endif
    }
    void hscLiveRecordingAndProperties() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QString path =
            QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../examples/hsc-coupled.bagua.json");
        QVERIFY(w.openPath(path));
        w.scene->select("soft");
        auto *young = w.findChild<QDoubleSpinBox *>("femYoungModulus");
        auto *enabled = w.findChild<QCheckBox *>("objectFem");
        auto *attachment = w.findChild<QComboBox *>("femAttachmentBody");
        QVERIFY(young && enabled && attachment);
        QVERIFY(enabled->isChecked());
        QCOMPARE(attachment->currentData().toString(), QString("ball"));
        young->setValue(6000);
        QVERIFY(QMetaObject::invokeMethod(young, "editingFinished", Qt::DirectConnection));
        QCOMPARE(w.scene->current()->youngModulus, 6000.);
        w.scene->undo.undo();
        QCOMPARE(w.scene->current()->youngModulus, 5000.);
        HscOptions options;
        options.enabled = true;
        options.nativePipeline = false;
        w.setPhysicsOptions(options);
        bool settingsChecked = false;
        QTimer::singleShot(100, &w, [&] {
            auto *dialog = w.findChild<QDialog *>();
            if (!dialog)
                return;
            auto *toggle = dialog->findChild<QCheckBox *>("hscEnabled");
            settingsChecked = toggle && toggle->isChecked();
            if (toggle)
                toggle->setChecked(false);
            QDir().mkpath("../artifacts");
            dialog->grab().save("../artifacts/hsc-settings-preview.png");
            dialog->reject();
        });
        auto *settings = w.findChild<QAction *>("physicsSettings");
        QVERIFY(settings);
        settings->trigger();
        QVERIFY(settingsChecked);
        QVERIFY(w.physicsOptions().enabled);
        QVERIFY(w.startSimulation());
        w.pauseSimulation();
        QVERIFY(!enabled->isEnabled());
        QVERIFY(w.beginRecording());
        QVERIFY(!w.simulationRunning());
        auto rest = w.simulationPreview()->find("soft")->mesh->positions;
        for (int i = 0; i < 30; ++i)
            QVERIFY(w.stepSimulation());
        w.endRecording();
        QCOMPARE(w.recordedFrames(), 31);
        QVERIFY(!w.seekReplay(.05));
        QVERIFY(w.simulationActive());
        QVERIFY(!w.replayActive());
        const auto expected = w.simulationPreview()->find("soft")->mesh->positions;
        QVERIFY((expected[16] - rest[16]).length() > 1e-5f);
        QTemporaryDir temp(QDir::currentPath() + "/hsc-live-XXXXXX");
        QVERIFY(w.saveRecording(temp.filePath("live.brec")));
        Recording saved;
        QString error;
        QVERIFY2(saved.load(temp.filePath("live.brec"), &error), qPrintable(error));
        QVERIFY(saved.metadata.value("hscEnabled").toBool());
        QVERIFY(saved.metadata.value("producer").toString().contains("HSC"));
        w.resetSimulation();
        QVERIFY(w.openRecording(temp.filePath("live.brec")));
        options.enabled = false;
        w.setPhysicsOptions(options);
        QVERIFY(w.seekReplay(saved.duration()));
        const auto actual = w.replayPreview()->find("soft")->mesh->positions;
        QCOMPARE(actual, expected);
        QVERIFY(!w.simulationActive());
        QVERIFY(w.close());
    }
    void hscMatchesReproductionReference() {
        QFile file(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("data/hsc-reference.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(file.readAll()).object();
        for (auto entry : root.value("cases").toArray()) {
            auto j = entry.toObject();
            auto vector = [](QJsonValue value) {
                std::vector<double> result;
                for (auto v : value.toArray())
                    result.push_back(v.toDouble());
                return result;
            };
            auto matrix = vector(j.value("matrix")), rhs = vector(j.value("rhs")),
                 expected = vector(j.value("expected"));
            HscOptions options;
            options.enabled = true;
            HscStats stats;
            std::vector<double> solution;
            QString error;
            QVERIFY2(solveHsc(matrix, j.value("femDofs").toInt(), {12, 12, 12}, rhs, options, solution, stats, &error),
                     qPrintable(error));
            QVERIFY(stats.rank > 0);
            QVERIFY(stats.relativeResidual <= options.tolerance);
            QVERIFY(stats.acaResidual < 1e-10);
            double norm = 0, difference = 0;
            for (size_t i = 0; i < solution.size(); ++i) {
                norm += expected[i] * expected[i];
                difference += (solution[i] - expected[i]) * (solution[i] - expected[i]);
            }
            QVERIFY(std::sqrt(difference / norm) < 1e-4);
            auto original = solution;
            matrix[0] = -1;
            QVERIFY(!solveHsc(matrix, j.value("femDofs").toInt(), {12, 12, 12}, rhs, options, solution, stats, &error));
            QCOMPARE(solution, original);
        }
    }
    void hscChronoCoupledToggle() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        Scene scene;
        QString error;
        QString path =
            QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../examples/hsc-coupled.bagua.json");
        QVERIFY2(scene.load(path, &error), qPrintable(error));
        const auto authored = scene.json();
        Physics baseline, hsc;
        HscOptions options;
        options.enabled = true;
        options.nativePipeline = false;
        QVERIFY2(baseline.start(scene, &error), qPrintable(error));
        QVERIFY2(hsc.start(scene, &error, options), qPrintable(error));
        QVERIFY(hsc.hscActive());
        QVERIFY(!baseline.hscActive());
        int peakRank = 0;
        const auto rest = hsc.preview.find("soft")->mesh->positions;
        for (int i = 0; i < 480; ++i) {
            QVERIFY2(baseline.step(1, &error), qPrintable(error));
            QVERIFY2(hsc.step(1, &error), qPrintable(error));
            peakRank = std::max(peakRank, hsc.hscStats().rank);
            QVERIFY(hsc.hscStats().relativeResidual <= options.tolerance);
            QVERIFY((baseline.preview.find("ball")->position - hsc.preview.find("ball")->position).length() < 1e-4f);
        }
        qInfo() << "HSC coupled 480 steps:" << "peak rank" << peakRank << "residual" << hsc.hscStats().relativeResidual
                << "asymmetry" << hsc.hscStats().asymmetry;
        QVERIFY(peakRank > 0);
        QVERIFY(hsc.hscStats().calls >= 480);
        const auto *a = baseline.preview.find("soft"), *b = hsc.preview.find("soft");
        QVERIFY(a->mesh && b->mesh);
        double moved = 0;
        for (qsizetype i = 0; i < a->mesh->positions.size(); ++i) {
            QVERIFY((a->mesh->positions[i] - b->mesh->positions[i]).length() < 2e-4f);
            moved = std::max(
                moved,
                double((b->mesh->positions[i] - rest[i]).length())); // actual displacement from the generated rest mesh
        }
        QVERIFY(moved > 0);
        QCOMPARE(scene.json(), authored);
        Scene rigid;
        rigid.reset();
        Physics unchanged;
        QVERIFY(unchanged.start(rigid, &error, options));
        QVERIFY(!unchanged.hscActive());
        QVERIFY(unchanged.solverDescription().contains(QStringLiteral("不适用")));
    }
    void physicsAndReplayControlsAreIndependent() {
        Scene source;
        source.reset(false);
        source.create(Shape::Box);
        Recording record;
        record.begin(source);
        source.objects[0].position.setX(2);
        QVERIFY(record.append(1, source));
        QTemporaryDir temp(QDir::currentPath() + "/control-separation-XXXXXX");
        QVERIFY(record.save(temp.filePath("run.brec")));
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openRecording(temp.filePath("run.brec")));
        QVERIFY(w.seekReplay(.4));
        auto *start = w.findChild<QAction *>("playSimulation");
        auto *stop = w.findChild<QAction *>("resetSimulation");
        QVERIFY(start && !start->isEnabled());
        QVERIFY(stop && !stop->isEnabled());
        QVERIFY(!w.startSimulation());
        QVERIFY(!w.stepSimulation());
        w.pauseSimulation();
        w.resetSimulation();
        QVERIFY(w.replayActive());
        QCOMPARE(w.replayTime(), .4);
        QVERIFY(!w.simulationActive());
        QCOMPARE(w.simulationTime(), 0.);
        w.playReplay();
        QTRY_VERIFY_WITH_TIMEOUT(w.replayTime() > .45, 2000);
        w.pauseReplay();
        QVERIFY(!w.simulationRunning());
        w.stopReplay();
        if (Physics::available()) {
            QVERIFY(start->isEnabled());
            QVERIFY(w.startSimulation());
            w.pauseSimulation();
            QVERIFY(!w.replayActive());
            w.resetSimulation();
        }
        QVERIFY(w.close());
    }
    void importedMeshChronoCollision() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        QTemporaryDir temp(QDir::currentPath() + "/mesh-physics-XXXXXX");
        QFile file(temp.filePath("cube.obj"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            "v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\nv -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv "
            "0.5 0.5 0.5\nv -0.5 0.5 0.5\nf 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 1 5 8 4\nf 2 3 7 6\n");
        file.close();
        QVector<Object> objects;
        QString error;
        QVERIFY(Assets::importFile(file.fileName(), objects, &error));
        Object dynamic = objects.first();
        dynamic.fixed = false;
        dynamic.position = {0, 3, 0};
        Object platform = objects.first();
        platform.id = "platform";
        platform.position = {3, 1, 0};
        Object ground;
        ground.id = "ground";
        ground.name = "ground";
        ground.shape = Shape::Plane;
        ground.fixed = true;
        ground.position = {0, 0, 0};
        ground.dimensions = {15, 1, 15};
        Object sphere;
        sphere.id = "sphere";
        sphere.name = "sphere";
        sphere.shape = Shape::Sphere;
        sphere.position = {3, 4, 0};
        Scene scene;
        scene.objects = {ground, dynamic, platform, sphere};
        Physics physics;
        QVERIFY2(physics.start(scene, &error), qPrintable(error));
        QVERIFY2(physics.step(960, &error), qPrintable(error));
        QVERIFY(std::abs(physics.preview.find(dynamic.id)->position.y() - .5f) < .03f);
        QVERIFY(std::abs(physics.preview.find("sphere")->position.y() - 2.f) < .03f);
        QCOMPARE(physics.preview.find("platform")->position, platform.position);
    }
    void meshImportAndPortableScene() {
        QTemporaryDir temp(QDir::currentPath() + "/asset-tests-XXXXXX");
        auto write = [&](QString name, QByteArray bytes) {
            QFile f(temp.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(bytes), bytes.size());
        };
        write("part.mtl", "newmtl red\nKd 1 0 0\n");
        write("part.obj", "mtllib part.mtl\no base\nv 0 0 0\nv 1000 0 0\nv 1000 1000 0\nv 0 1000 0\nusemtl red\nf -4 "
                          "-3 -2 -1\no second\nf 1 3 4\n");
        QVector<Object> objects;
        QString error;
        QVERIFY2(Assets::importFile(temp.filePath("part.obj"), objects, &error, {.001, true}), qPrintable(error));
        QCOMPARE(objects.size(), 2);
        QCOMPARE(objects[0].mesh->indices.size(), 6);
        QCOMPARE(objects[0].color, QColor(Qt::red));
        auto world = objects[0].matrix().map(objects[0].mesh->positions[2]);
        QVERIFY((world - QVector3D(1, 0, -1)).length() < 1e-5f);
        QVERIFY(objects[0].fixed);
        QCOMPARE(objects[0].mesh->sha256.size(), 64);
        Scene scene;
        scene.objects = objects;
        scene.selected = objects[0].id;
        scene.duplicateSelected();
        QCOMPARE(scene.objects.size(), 3);
        QCOMPARE(scene.json().value("assets").toObject().size(), 2); // instances reuse mesh data
        QVERIFY(scene.save(temp.filePath("portable.bagua.json")));
        QVERIFY(QFile::remove(temp.filePath("part.obj")));
        QVERIFY(QFile::remove(temp.filePath("part.mtl")));
        Scene restored;
        QVERIFY2(restored.load(temp.filePath("portable.bagua.json"), &error), qPrintable(error));
        QCOMPARE(restored.json(), scene.json());
        QVERIFY(std::isfinite(intersect(restored.objects[0], {.5f, 3, -.5f}, {0, -1, 0})));
        write("triangle.stl", "solid test\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 "
                              "0\nendloop\nendfacet\nendsolid\n");
        QVERIFY(Assets::importFile(temp.filePath("triangle.stl"), objects, &error));
        QCOMPARE(objects[0].mesh->indices.size(), 3);
        QByteArray binary(80, 0);
        QDataStream ds(&binary, QIODevice::Append);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
        ds << quint32(1);
        for (float v : {0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f})
            ds << v;
        ds << quint16(0);
        write("binary.stl", binary);
        QVERIFY2(Assets::importFile(temp.filePath("binary.stl"), objects, &error), qPrintable(error));
        write("triangle.ply",
              "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement "
              "face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n");
        QVERIFY(Assets::importFile(temp.filePath("triangle.ply"), objects, &error));
        const auto valid = objects[0].json();
        write("bad.obj", "v 0 0 0\nf 1 2 3\n");
        QVERIFY(!Assets::importFile(temp.filePath("bad.obj"), objects, &error));
        QCOMPARE(objects[0].json(), valid);
        auto invalid = scene.json();
        auto assets = invalid.value("assets").toObject();
        auto mesh = assets.begin().value().toObject();
        mesh["indices"] = QJsonArray{999999, 0, 1};
        assets[mesh.value("id").toString()] = mesh;
        invalid["assets"] = assets;
        const auto before = restored.json();
        QVERIFY(!restored.loadJson(invalid, &error));
        QCOMPARE(restored.json(), before);
    }
    void recordingRoundTripAndDeformation() {
        Scene scene;
        scene.reset(false);
        scene.create(Shape::Box);
        Object meshObject;
        meshObject.id = "mesh";
        meshObject.name = "deform";
        meshObject.shape = Shape::Mesh;
        auto mesh = std::make_shared<MeshData>();
        mesh->id = "asset";
        mesh->name = "triangle";
        mesh->positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        mesh->indices = {0, 1, 2};
        meshObject.mesh = mesh;
        scene.objects.push_back(meshObject);
        Recording recording;
        recording.begin(scene, {{"producer", "test"}});
        scene.objects[0].position = {2, 3, 4};
        scene.objects[0].rotation = {0, 90, 0};
        auto changed = std::make_shared<MeshData>(*mesh);
        changed->positions[2].setY(2);
        scene.objects[1].mesh = changed;
        QVERIFY(recording.append(1, scene));
        QVERIFY(!recording.append(.5, scene));
        QTemporaryDir temp(QDir::currentPath() + "/recording-tests-XXXXXX");
        QString path = temp.filePath("run.brec"), error;
        QVERIFY2(recording.save(path, &error), qPrintable(error));
        QVERIFY(!recording.dirty);
        Recording loaded;
        QVERIFY2(loaded.load(path, &error), qPrintable(error));
        QCOMPARE(loaded.frameCount(), 2);
        QCOMPARE(loaded.duration(), 1.);
        QVector<Object> state;
        QVERIFY(loaded.sample(.5, state, &error));
        QVERIFY((state[0].position - QVector3D(1, 1.75f, 2)).length() < 1e-5f);
        QCOMPARE(state[1].mesh->positions[2].y(), 1.5f);
        QVERIFY(loaded.sample(.5, state, &error, false));
        QCOMPARE(state[1].mesh->positions[2].y(), 1.f);
        QVERIFY(loaded.sample(100, state));
        QCOMPARE(state[0].position, QVector3D(2, 3, 4));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto bytes = f.readAll();
        f.close();
        QFile bad(temp.filePath("bad.brec"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write(bytes.left(bytes.size() - 8));
        bad.close();
        QVERIFY(!loaded.load(bad.fileName(), &error));
        QCOMPARE(loaded.frameCount(), 2);
        QVERIFY(loaded.frame(1, state));
        QCOMPARE(state[1].mesh->positions[2].y(), 2.f);
        QVERIFY(loaded.save(temp.filePath("copy.brec"), &error));
        Recording copy;
        QVERIFY(copy.load(temp.filePath("copy.brec")));
        QVERIFY(copy.frame(1, state));
        QCOMPARE(state[0].position, QVector3D(2, 3, 4));
    }
    void hscRealSnapshotsReplay() {
        const QString root = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../examples/HSC");
        QFile vf(root + "/validation.json");
        QVERIFY(vf.open(QIODevice::ReadOnly));
        const auto validation = QJsonDocument::fromJson(vf.readAll()).array();
        for (const auto &entry : validation) {
            auto j = entry.toObject();
            Recording recording;
            QString error;
            QVERIFY2(recording.load(root + "/" + j.value("recording").toString(), &error), qPrintable(error));
            QCOMPARE(recording.frameCount(), j.value("saved_frames").toInt());
            QVERIFY(std::abs(recording.duration() - j.value("duration").toDouble()) < 1e-9);
            QCOMPARE(recording.initial.objects.size(), 20);
            QCOMPARE(recording.metadata.value("solver").toString(), QString("linear_hsc"));
            QVector<Object> state;
            // Decode every saved frame to catch corrupt middle chunks and topology mismatches.
            for (int i = 0; i < recording.frameCount(); ++i)
                QVERIFY2(recording.frame(i, state, &error), qPrintable(error));
            for (auto check : j.value("checks").toArray()) {
                auto c = check.toObject();
                QVERIFY(recording.frame(c.value("frame_index").toInt(), state, &error));
                for (auto pair : {std::pair<QString, QString>{"chassis", "chassis_vertex_0"},
                                  {"wheel_0_tire", "tire_vertex_100"}}) {
                    auto it =
                        std::find_if(state.cbegin(), state.cend(), [&](const Object &o) { return o.id == pair.first; });
                    QVERIFY(it != state.cend());
                    int index = pair.first == "chassis" ? 0 : 100;
                    auto world = it->matrix().map(it->mesh->positions[index]);
                    auto expected = c.value(pair.second).toArray();
                    QVERIFY2((world - QVector3D(float(expected[0].toDouble()), float(expected[1].toDouble()),
                                                float(expected[2].toDouble())))
                                     .length() < 2e-5f,
                             qPrintable(pair.first));
                }
            }
        }
    }
    void recordingListKeepsEachTake() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        if (!Physics::available()) {
            QVERIFY(w.close());
            return;
        }
        w.newScene(false);
        w.scene->create(Shape::Box);
        w.scene->undo.setClean();
        QVERIFY(w.startSimulation());
        // First take.
        QVERIFY(w.beginRecording());
        for (int i = 0; i < 4; ++i)
            QVERIFY(w.stepSimulation());
        w.endRecording();
        const int first = w.recordedFrames();
        QVERIFY(first > 1);
        // A second take must be added next to it, not overwrite it.
        QVERIFY(w.beginRecording());
        for (int i = 0; i < 3; ++i)
            QVERIFY(w.stepSimulation());
        w.endRecording();
        QCOMPARE(w.recordingCount(), 2);
        QCOMPARE(w.recordedFrames(), 4);
        auto *list = w.findChild<QListWidget *>("recordingList");
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentRow(), 1);
        QTemporaryDir temp(QDir::currentPath() + "/takes-XXXXXX");
        // Each entry can be replayed, saved and deleted on its own.
        QVERIFY(w.selectRecordingAt(0));
        QCOMPARE(w.recordedFrames(), first);
        QCOMPARE(list->currentRow(), 0);
        w.resetSimulation();   // replay needs the runtime detached first
        QVERIFY(w.seekReplay(0));
        QVERIFY(w.replayActive());
        w.stopReplay();
        QVERIFY(w.saveRecordingAt(0, temp.filePath("take1.brec")));
        w.deleteRecordingAt(0);
        QCOMPARE(w.recordingCount(), 1);
        QCOMPARE(list->count(), 1);
        QCOMPARE(w.recordedFrames(), 4);
        QVERIFY(w.saveRecordingAt(0, temp.filePath("take2.brec")));
        Recording firstTake, secondTake;
        QString error;
        QVERIFY2(firstTake.load(temp.filePath("take1.brec"), &error), qPrintable(error));
        QVERIFY2(secondTake.load(temp.filePath("take2.brec"), &error), qPrintable(error));
        QCOMPARE(firstTake.frameCount(), first);
        QCOMPARE(secondTake.frameCount(), 4);
        QVERIFY(firstTake.times.last() > secondTake.times.last());
        QVERIFY(w.close());
    }
    void recordingUiAndPortableProject() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        if (Physics::available()) {
            QVERIFY(!w.beginRecording());
            QVERIFY(w.startSimulation());
            QVERIFY(w.beginRecording());
            QTRY_VERIFY_WITH_TIMEOUT(w.recordedFrames() > 3, 3000);
            w.endRecording();
            w.pauseSimulation();
            QTemporaryDir temp(QDir::currentPath() + "/project-tests-XXXXXX");
            const auto initial = w.scene->json();
            int count = w.recordedFrames();
            QVERIFY(w.saveRecording(temp.filePath("run.brec")));
            w.resetSimulation();
            QVERIFY(w.openRecording(temp.filePath("run.brec")));
            QCOMPARE(w.recordedFrames(), count);
            QVERIFY(w.seekReplay(.01));
            QVERIFY(!w.viewport->editingEnabled);
            w.playReplay();
            w.pauseReplay();
            auto *slider = w.findChild<QSlider *>("replayTimeline");
            QVERIFY(slider);
            slider->setValue(100000);
            QVERIFY(w.replayTime() > 0);
            QVERIFY(w.saveProject(temp.filePath("project.bagua.project.json")));
            MainWindow portable;
            QVERIFY(portable.openProject(temp.filePath("project.bagua.project.json")));
            QVERIFY(portable.recordedFrames() == count);
            QVERIFY(portable.replayActive());
            QCOMPARE(portable.findChild<QTabBar *>("sceneTabs")->count(),
                     w.findChild<QTabBar *>("sceneTabs")->count() + 1);
            QVERIFY(std::abs(portable.replayTime() - w.replayTime()) < 1e-8);
            QCOMPARE(portable.scene->json().value("objects"), initial.value("objects"));
            w.stopReplay();
            QVERIFY(w.viewport->editingEnabled);
            QVERIFY(w.close());
            QVERIFY(portable.close());
        } else
            QVERIFY(w.close());
    }
    void simulateAndRecordFixedStep() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        if (!Physics::available()) {
            QVERIFY(w.close());
            return;
        }
        w.newScene(false);
        w.scene->create(Shape::Box);
        // The close path at the end must not prompt for an unsaved scene.
        w.scene->undo.setClean();
        auto *offline = w.findChild<QCheckBox *>("recordOffline");
        auto *startAndRecord = w.findChild<QAction *>("recordWithSimulation");
        QVERIFY(offline && startAndRecord);
        QVERIFY(!w.simulationActive());
        // Offline recording must start the simulation and capture on its own.
        offline->setChecked(true);
        startAndRecord->trigger();
        QVERIFY(w.simulationActive());
        QVERIFY(w.recordedFrames() >= 1);
        QVERIFY(!startAndRecord->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(w.recordedFrames() > 8, 5000);
        w.endRecording();
        w.pauseSimulation();
        QTemporaryDir temp(QDir::currentPath() + "/fixed-step-XXXXXX");
        QVERIFY(w.saveRecording(temp.filePath("fixed.brec")));
        Recording loaded;
        QString error;
        QVERIFY2(loaded.load(temp.filePath("fixed.brec"), &error), qPrintable(error));
        QVERIFY2(loaded.frameCount() > 8, qPrintable(QString::number(loaded.frameCount())));
        QCOMPARE(loaded.metadata.value("sampling").toString(), QStringLiteral("lock-step fixed timestep"));
        // Lock-step capture records exactly one integration step per frame, so no frame is
        // thinned out by a slow tick the way render-tick sampling would.
        for (int i = 1; i < loaded.frameCount(); ++i) {
            const double delta = loaded.times[i] - loaded.times[i - 1];
            QVERIFY2(std::abs(delta - Physics::timeStep) < 1e-9,
                     qPrintable(QStringLiteral("frame %1 delta %2 s").arg(i).arg(delta, 0, 'g', 12)));
        }
        QVERIFY(w.close());
    }
    void replayTransportAndTabPause() {
        Scene source;
        source.reset(false);
        source.create(Shape::Box);
        Recording recording;
        recording.begin(source);
        source.objects[0].position = {4, 2, 0};
        QVERIFY(recording.append(1, source));
        QTemporaryDir temp(QDir::currentPath() + "/replay-ui-XXXXXX");
        QVERIFY(recording.save(temp.filePath("run.brec")));
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openRecording(temp.filePath("run.brec")));
        w.playReplay();
        QTRY_VERIFY_WITH_TIMEOUT(w.replayTime() > .04, 2000);
        w.pauseReplay();
        auto paused = w.replayTime();
        QTest::qWait(60);
        QCOMPARE(w.replayTime(), paused);
        QVERIFY(!w.replayRunning());
        auto *speed = w.findChild<QComboBox *>("replaySpeed");
        speed->setCurrentIndex(4);
        w.findChild<QCheckBox *>("replayLoop")->setChecked(true);
        QVERIFY(w.seekReplay(.98));
        w.playReplay();
        QTRY_VERIFY_WITH_TIMEOUT(w.replayTime() < .8, 2000);
        QVERIFY(w.replayRunning());
        auto *model = w.scene;
        auto *tabs = w.findChild<QTabBar *>("sceneTabs");
        int index = tabs->currentIndex();
        w.newScene();
        QVERIFY(!w.replayActive());
        tabs->setCurrentIndex(index);
        QCOMPARE(w.scene, model);
        QVERIFY(!w.replayRunning());
        paused = w.replayTime();
        QTest::qWait(60);
        QCOMPARE(w.replayTime(), paused);
        w.stopReplay();
        QVERIFY(w.viewport->editingEnabled);
        const auto authoredId = w.scene->create(Shape::Sphere);
        const auto authoredCount = w.scene->objects.size();
        QVERIFY(w.seekReplay(0));
        auto *tree = w.findChild<QTreeWidget *>("sceneTree");
        QCOMPARE(tree->topLevelItemCount(), 1);
        tree->setCurrentItem(tree->topLevelItem(0));
        QVERIFY(w.replayPreview()->current() != nullptr);
        QCOMPARE(w.scene->selected, authoredId);
        w.stopReplay();
        QCOMPARE(w.scene->objects.size(), authoredCount);
        QCOMPARE(tree->topLevelItemCount(), authoredCount);
        w.scene->undo.setClean();
        QVERIFY(w.close());
    }
    void chronoUprightCylinderStability() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        Scene s;
        Object ground;
        ground.id = "ground";
        ground.name = "ground";
        ground.shape = Shape::Plane;
        ground.position = {0, 0, 0};
        ground.dimensions = {30, 1, 30};
        ground.fixed = true;
        s.objects.push_back(ground);
        // The starter scene's cylinder is 1.1 m wide and 1.8 m tall.
        // Include yawed cylinders and the nonuniform scale path, all initially resting on the floor.
        for (int i = 0; i < 4; ++i) {
            Object o;
            o.id = QString::number(i);
            o.name = o.id;
            o.shape = Shape::Cylinder;
            o.dimensions = {1.1f, 1.8f, 1.1f};
            o.position = {float(i * 4 - 6), .9f, 0};
            o.rotation = {0, float(i * 23), 0};
            if (i == 3)
                o.scale = {1.2f, 1, .8f};
            s.objects.push_back(o);
        }
        Physics p;
        QString error;
        QVERIFY2(p.start(s, &error), qPrintable(error));
        // Check throughout 30 simulated seconds, not only the final pose.
        for (int second = 0; second < 30; ++second) {
            QVERIFY2(p.step(240, &error), qPrintable(error));
            for (int i = 0; i < 4; ++i) {
                const auto *o = p.preview.find(QString::number(i));
                const auto up = QQuaternion::fromEulerAngles(o->rotation).rotatedVector({0, 1, 0});
                QVERIFY2(up.y() > .999f, qPrintable(QString("cylinder %1 at %2 s: upY=%3, y=%4")
                                                        .arg(i)
                                                        .arg(second + 1)
                                                        .arg(up.y())
                                                        .arg(o->position.y())));
                QVERIFY2(std::abs(o->position.y() - .9f) < .02f,
                         qPrintable(QString("cylinder %1 at %2 s: y=%3").arg(i).arg(second + 1).arg(o->position.y())));
                QVERIFY((QVector2D(o->position.x(), o->position.z()) - QVector2D(float(i * 4 - 6), 0)).length() < .02f);
            }
        }
    }
    void physicalPropertiesPersistence() {
        Scene s;
        s.reset();
        Object o = *s.current();
        o.collisionEnabled = false;
        o.fixed = true;
        o.mass = 7.25;
        s.edit(o, "physics");
        s.undo.undo();
        QVERIFY(s.current()->collisionEnabled);
        s.undo.redo();
        Scene restored;
        QString error;
        QVERIFY(restored.loadJson(s.json(), &error));
        QCOMPARE(restored.current()->json(), o.json());
        s.duplicateSelected();
        QVERIFY(!s.current()->collisionEnabled);
        QCOMPARE(s.current()->mass, 7.25);
        auto legacy = s.json();
        auto objects = legacy.value("objects").toArray();
        for (int i = 0; i < objects.size(); ++i) {
            auto j = objects[i].toObject();
            j.remove("mass");
            j.remove("fixed");
            j.remove("collisionEnabled");
            objects[i] = j;
        }
        legacy["objects"] = objects;
        QVERIFY(restored.loadJson(legacy, &error));
        QVERIFY(restored.find("ground")->fixed);
        QVERIFY(restored.current()->collisionEnabled);
        QCOMPARE(restored.current()->mass, 1.0);
        const auto valid = restored.json();
        auto j = objects[0].toObject();
        j["mass"] = 0;
        objects[0] = j;
        legacy["objects"] = objects;
        QVERIFY(!restored.loadJson(legacy, &error));
        QCOMPARE(restored.json(), valid);
        j["mass"] = 1;
        j["collisionEnabled"] = "yes";
        objects[0] = j;
        legacy["objects"] = objects;
        QVERIFY(!restored.loadJson(legacy, &error));
        QCOMPARE(restored.json(), valid);
    }
    void chronoFreeFallAndContacts() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        Scene s;
        Object ground;
        ground.id = "ground";
        ground.name = "ground";
        ground.shape = Shape::Plane;
        ground.position = {0, 0, 0};
        ground.dimensions = {20, 1, 20};
        ground.fixed = true;
        s.objects.push_back(ground);
        for (int i = 0; i < 4; ++i) {
            Object o;
            o.id = QString::number(i);
            o.name = o.id;
            o.shape = Shape(i);
            o.position = {float(i * 3 - 4), 4, 0};
            if (i == 3) {
                o.shape = Shape::Box;
                o.collisionEnabled = false;
            }
            if (i == 1)
                o.dimensions = {1.5f, 1, 1.2f}; // ellipsoid
            if (i == 2)
                o.dimensions = {1.2f, 2, .8f}; // elliptical cylinder hull
            s.objects.push_back(o);
        }
        Object fixed;
        fixed.id = "fixed";
        fixed.name = "fixed";
        fixed.fixed = true;
        fixed.position = {0, 6, 5};
        s.objects.push_back(fixed);
        const auto before = s.json();
        Physics p;
        QString error;
        QVERIFY2(p.start(s, &error), qPrintable(error));
        QVERIFY2(p.step(120, &error), qPrintable(error));
        QVERIFY(std::abs(p.preview.find("3")->position.y() - (4 - 9.81 * .5 * .5 / 2)) < .02);
        QVERIFY2(p.step(840, &error), qPrintable(error));
        QVERIFY(std::abs(p.time() - 4) < 1e-9);
        for (int i = 0; i < 3; ++i) {
            const auto *o = p.preview.find(QString::number(i));
            QVERIFY2(o->position.y() > .35 && o->position.y() < 1.2,
                     qPrintable(QString("body %1 y=%2").arg(i).arg(o->position.y())));
        }
        QVERIFY(p.contacts() > 0);
        QVERIFY(p.preview.find("3")->position.y() < -50);
        QCOMPARE(p.preview.find("fixed")->position, fixed.position);
        QCOMPARE(s.json(), before);
        // A ground without a collider cannot support a collidable body.
        s.objects[0].collisionEnabled = false;
        QVERIFY(p.start(s, &error));
        QVERIFY(p.step(480, &error));
        QVERIFY(p.preview.find("0")->position.y() < -10);
        s.objects[1].mass = 0;
        const double previousTime = p.time();
        QVERIFY(!p.start(s, &error));
        QCOMPARE(p.time(), previousTime); // transactional initialization
        p.reset();
        QVERIFY(!p.active());
        QCOMPARE(p.time(), 0.0);
    }
    void simulationControlsAndTabs() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *collision = w.findChild<QCheckBox *>("objectCollision");
        QVERIFY(collision);
        QVERIFY(collision->isChecked());
        Object precise = *w.scene->current();
        precise.mass = 1.234567;
        w.scene->edit(precise, "precise mass");
        collision->click();
        QVERIFY(!w.scene->current()->collisionEnabled);
        QCOMPARE(w.scene->current()->mass, precise.mass);
        w.scene->undo.undo();
        QVERIFY(collision->isChecked());
        Object o = *w.scene->current();
        o.position.setY(4);
        w.scene->edit(o, "raise");
        w.scene->undo.setClean();
        const auto authored = w.scene->json();
        const int undoIndex = w.scene->undo.index();
        QVERIFY(w.startSimulation());
        QVERIFY(!w.viewport->editingEnabled);
        QVERIFY(!collision->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(w.simulationTime() > .05, 3000);
        w.pauseSimulation();
        const auto t = w.simulationTime();
        QTest::qWait(60);
        QCOMPARE(w.simulationTime(), t);
        QVERIFY(w.stepSimulation());
        QVERIFY(w.simulationTime() > t);
        QVERIFY(w.simulationPreview()->current()->position.y() < 4);
        const double firstTime = w.simulationTime();
        QTemporaryDir temp(QDir::currentPath() + "/physics-tests-XXXXXX");
        QVERIFY(w.savePath(temp.filePath("original.bagua.json")));
        Scene saved;
        QVERIFY(saved.load(temp.filePath("original.bagua.json")));
        QCOMPARE(saved.current()->position, o.position);
        QVERIFY(w.startSimulation());
        auto *first = w.scene;
        w.newScene();
        QVERIFY(!w.simulationActive());
        QVERIFY(!w.simulationRunning());
        QVERIFY(w.viewport->editingEnabled);
        w.findChild<QTabBar *>("sceneTabs")->setCurrentIndex(0);
        QCOMPARE(w.scene, first);
        QVERIFY(w.simulationActive());
        QVERIFY(!w.simulationRunning());
        QVERIFY(w.simulationTime() >= firstTime);
        w.resetSimulation();
        QVERIFY(!w.simulationActive());
        QVERIFY(w.viewport->editingEnabled);
        QVERIFY(collision->isEnabled());
        QCOMPARE(w.scene->json().value("objects"), authored.value("objects"));
        QCOMPARE(w.scene->undo.index(), undoIndex);
        QVERIFY(w.scene->undo.isClean());
        QVERIFY(w.close());
    }
    void chronoScaledRotatedCollider() {
        if (!Physics::available())
            QSKIP("Chrono disabled");
        Scene s;
        Object obstacle;
        obstacle.id = "obstacle";
        obstacle.name = "obstacle";
        obstacle.fixed = true;
        obstacle.position = {0, 2, 0};
        obstacle.dimensions = {3, 1, 3};
        obstacle.scale = {2, 1, 1};
        obstacle.rotation = {0, 0, 90};
        Object ball;
        ball.id = "ball";
        ball.name = "ball";
        ball.shape = Shape::Sphere;
        ball.position = {0, 7, 0};
        s.objects = {obstacle, ball};
        Physics p;
        QString error;
        QVERIFY2(p.start(s, &error), qPrintable(error));
        QVERIFY2(p.step(720, &error), qPrintable(error));
        // Rotating the scaled six-metre X dimension puts the top face at world Y=5.
        QVERIFY2(std::abs(p.preview.find("ball")->position.y() - 5.5f) < .03f,
                 qPrintable(QString::number(p.preview.find("ball")->position.y())));
        QCOMPARE(p.preview.find("obstacle")->position, obstacle.position);
    }
    void sceneUndoAndPersistence() {
        Scene scene;
        scene.reset(false);
        QString id = scene.create(Shape::Cylinder);
        QCOMPARE(scene.objects.size(), 1);
        Object o = *scene.current();
        o.name = QStringLiteral("试验圆柱");
        o.position = {2, 3, -4};
        o.rotation = {23, -41, 16};
        o.scale = {2, .5f, 3};
        o.dimensions = {1.2f, 4, 2.1f};
        o.color = QColor("#ab37e5");
        o.visible = false;
        scene.edit(o, "properties");
        scene.undo.undo();
        QVERIFY(scene.current()->visible);
        scene.undo.redo();
        QCOMPARE(scene.current()->json(), o.json());
        scene.duplicateSelected();
        QCOMPARE(scene.objects.size(), 2);
        QString copy = scene.selected;
        QVERIFY(copy != id);
        scene.removeSelected();
        QCOMPARE(scene.objects.size(), 1);
        scene.undo.undo();
        QCOMPARE(scene.selected, copy);
        scene.undo.undo();
        QCOMPARE(scene.objects.size(), 1);
        QTemporaryDir temp(QDir::currentPath() + "/scene-tests-XXXXXX");
        QString path = temp.filePath("scene.bagua.json"), error;
        QVERIFY2(scene.save(path, &error), qPrintable(error));
        QVERIFY(scene.undo.isClean());
        Scene restored;
        QVERIFY2(restored.load(path, &error), qPrintable(error));
        QCOMPARE(restored.json(), scene.json());
    }
    void malformedLoadIsTransactional() {
        Scene s;
        s.reset();
        const auto original = s.json();
        auto invalid = original;
        auto objects = invalid["objects"].toArray();
        objects.append(objects[0]);
        invalid["objects"] = objects;
        QString error;
        QVERIFY(!s.loadJson(invalid, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(s.json(), original);
        objects = original["objects"].toArray();
        auto first = objects[0].toObject();
        first["scale"] = QJsonArray{1, 0, 1};
        objects[0] = first;
        invalid["objects"] = objects;
        QVERIFY(!s.loadJson(invalid, &error));
        QCOMPARE(s.json(), original);
    }
    void geometryPicking() {
        Object o;
        o.shape = Shape::Sphere;
        o.position = {0, 0, 0};
        o.scale = {2, 1, 1};
        QVERIFY(std::isfinite(intersect(o, {0, 0, 5}, {0, 0, -1})));
        QVERIFY(!std::isfinite(intersect(o, {2, 2, 5}, {0, 0, -1})));
        for (int i = 0; i < 4; ++i) {
            auto mesh = geometry(Shape(i));
            QVERIFY(!mesh.isEmpty());
            QCOMPARE(mesh.size() % 3, 0);
        }
    }
    void backendIntegration() {
        Scene s;
        s.reset();
        auto r = validateBackends(s);
        if (r.cudaCompiled) {
            QVERIFY2(r.cudaAvailable, qPrintable(r.cuda));
            QVERIFY2(r.cudaPassed, qPrintable(r.cuda));
        }
        if (r.chronoCompiled) {
            QVERIFY2(r.chronoPassed, qPrintable(r.chrono));
            QCOMPARE(r.bodies, s.objects.size());
        }
    }
    void editorInteraction() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(500);
        auto *v = w.viewport;
        QVERIFY(v->isValid());
        // Tree <-> viewport selection, with actual triangle picking.
        w.scene->select("sphere-01");
        QCOMPARE(w.findChild<QTreeWidget *>("sceneTree")->currentItem()->data(0, Qt::UserRole).toString(),
                 QString("sphere-01"));
        auto center = v->project(w.scene->current()->position);
        QCOMPARE(v->pick(center), QString("sphere-01"));
        w.scene->select({});
        QTest::mouseClick(v, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QCOMPARE(w.scene->selected, QString("sphere-01"));
        auto *tree = w.findChild<QTreeWidget *>("sceneTree");
        tree->setCurrentItem(tree->topLevelItem(1));
        QCOMPARE(w.scene->selected, QString("box-01"));
        // Every gizmo gesture creates exactly one undo command, and can be cancelled.
        for (int t = 0; t < 3; ++t)
            for (int axis = 0; axis < 3; ++axis) {
                v->setTool(Tool(t));
                QTest::qWait(40);
                const auto old = w.scene->current()->json();
                const int index = w.scene->undo.index();
                QPoint start = v->handlePoint(axis).toPoint();
                QTest::mousePress(v, Qt::LeftButton, Qt::NoModifier, start);
                QVERIFY(v->dragging());
                QPoint end = start + QPoint(43, -27);
                moveMouse(v, end);
                QTest::mouseRelease(v, Qt::LeftButton, Qt::NoModifier, end);
                QVERIFY2(w.scene->current()->json() != old, qPrintable(QString("tool %1 did not change").arg(t)));
                QCOMPARE(w.scene->undo.index(), index + 1);
                w.scene->undo.undo();
                QCOMPARE(w.scene->current()->json(), old);
                w.scene->undo.redo();
                w.scene->undo.undo();
            }
        v->setTool(Tool::Move);
        const auto before = w.scene->current()->json();
        auto start = v->handlePoint(0).toPoint();
        QTest::mousePress(v, Qt::LeftButton, Qt::NoModifier, start);
        moveMouse(v, start + QPoint(60, 0));
        QTest::keyClick(v, Qt::Key_Escape);
        QTest::mouseRelease(v, Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(w.scene->current()->json(), before);
        // Inspector numeric editing / rename / visibility are all undoable.
        auto *x = w.findChild<QDoubleSpinBox *>("field_0_0");
        x->setFocus();
        x->selectAll();
        QTest::keyClicks(x, "3.25");
        QTest::keyClick(x, Qt::Key_Return);
        QCOMPARE(w.scene->current()->position.x(), 3.25f);
        w.scene->undo.undo();
        auto *name = w.findChild<QLineEdit *>("objectName");
        name->setText("Renamed Box");
        QMetaObject::invokeMethod(name, "editingFinished");
        QCOMPARE(w.scene->current()->name, QString("Renamed Box"));
        w.scene->undo.undo();
        auto *visible = w.findChild<QCheckBox *>("objectVisible");
        visible->click();
        QVERIFY(!w.scene->current()->visible);
        w.scene->undo.undo();
        QVERIFY(w.scene->current()->visible);
        tree->currentItem()->setText(0, "Tree Rename");
        QCOMPARE(w.scene->current()->name, QString("Tree Rename"));
        w.scene->undo.undo();
        v->setFocus();
        QTest::keyClick(v, Qt::Key_E);
        QCOMPARE(v->tool, Tool::Rotate);
        QTest::keyClick(v, Qt::Key_R);
        QCOMPARE(v->tool, Tool::Scale);
        QTest::keyClick(v, Qt::Key_W);
        QCOMPARE(v->tool, Tool::Move);
        auto camera = v->cameraJson();
        QTest::mousePress(v, Qt::RightButton, Qt::NoModifier, {200, 180});
        moveMouse(v, {250, 200}, Qt::RightButton);
        QTest::mouseRelease(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        QVERIFY(v->cameraJson() != camera);
        auto target = v->target;
        QTest::mousePress(v, Qt::MiddleButton, Qt::NoModifier, {200, 180});
        moveMouse(v, {230, 205}, Qt::MiddleButton);
        QTest::mouseRelease(v, Qt::MiddleButton, Qt::NoModifier, {230, 205});
        QVERIFY(v->target != target);
        float oldDistance = v->distance;
        QWheelEvent wheel(QPointF(300, 200), QPointF(v->mapToGlobal(QPoint(300, 200))), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &wheel);
        QVERIFY(v->distance < oldDistance);
        QTest::keyClick(v, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(w.scene->objects.size(), 5);
        QTest::keyClick(v, Qt::Key_Delete);
        QCOMPARE(w.scene->objects.size(), 4);
        QTest::keyClick(v, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(w.scene->objects.size(), 5);
        QTest::keyClick(v, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(w.scene->objects.size(), 4);
        QTemporaryDir temp(QDir::currentPath() + "/scene-tests-XXXXXX");
        QString path = temp.filePath("roundtrip.bagua.json");
        w.scene->select("box-01");
        auto *pendingName = w.findChild<QLineEdit *>("objectName");
        pendingName->setFocus();
        pendingName->selectAll();
        QTest::keyClicks(pendingName, "Saved pending edit");
        QVERIFY(w.savePath(path));
        QCOMPARE(w.scene->current()->name, QString("Saved pending edit"));
        auto scene = w.scene->json();
        w.newScene();
        QVERIFY(w.openPath(path));
        QCOMPARE(w.scene->json(), scene);
        QCOMPARE(v->cameraJson(), scene["view"].toObject());
        auto framebuffer = v->grabFramebuffer();
        QVERIFY(!framebuffer.isNull());
        QVERIFY(framebuffer.width() > 300);
        // The composite contains the green Y-axis handle, not just 3D objects.
        w.scene->select("box-01");
        v->home();
        v->setTool(Tool::Move);
        auto overlayImage = v->grabFramebuffer();
        const auto axisPoint = v->handlePoint(1) * v->devicePixelRatioF();
        bool greenHandle = false;
        for (int y = -4; y <= 4; ++y)
            for (int dx = -4; dx <= 4; ++dx) {
                auto pixel = overlayImage.pixelColor(qRound(axisPoint.x()) + dx, qRound(axisPoint.y()) + y);
                if (pixel.green() > 100 && pixel.green() > pixel.red() * 1.2 && pixel.green() > pixel.blue() * 1.1)
                    greenHandle = true;
            }
        QVERIFY(greenHandle);
        w.scene->undo.setClean();
        w.close();
    }
    void sceneTabsKeepIndependentDocuments() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *tabs = w.findChild<QTabBar *>("sceneTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        auto *viewport = w.viewport;
        auto *first = w.scene;
        viewport->yaw = 61;
        viewport->distance = 13;
        const auto firstCamera = viewport->cameraJson();
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(tabs->count(), 2);
        auto *second = w.scene;
        QVERIFY(first != second);
        second->create(Shape::Sphere);
        viewport->yaw = -30;
        viewport->setTool(Tool::Scale);
        const auto secondCamera = viewport->cameraJson();
        QTest::mouseClick(w.findChild<QToolButton *>("newSceneTabButton"), Qt::LeftButton);
        QCOMPARE(tabs->count(), 3);
        auto *third = w.scene;
        third->create(Shape::Cylinder);
        tabs->setCurrentIndex(0);
        QCOMPARE(w.scene, first);
        QCOMPARE(viewport->cameraJson(), firstCamera);
        auto *name = w.findChild<QLineEdit *>("objectName");
        name->setFocus();
        name->setText("Pending scene A edit");
        tabs->setCurrentIndex(1);
        QCOMPARE(w.scene, second);
        QCOMPARE(first->current()->name, QString("Pending scene A edit"));
        QCOMPARE(viewport->cameraJson(), secondCamera);
        QCOMPARE(viewport->tool, Tool::Scale);
        QCOMPARE(w.findChild<QTreeWidget *>("sceneTree")->topLevelItemCount(), 1);
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_Z, Qt::ControlModifier);
        QVERIFY(second->objects.isEmpty());
        QCOMPARE(first->objects.size(), 4);
        QCOMPARE(third->objects.size(), 1);
        QTest::keyClick(viewport, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(second->objects.size(), 1);
        tabs->moveTab(1, 2);
        QCOMPARE(w.scene, second);
        QCOMPARE(tabs->currentIndex(), 2);
        tabs->setCurrentIndex(1);
        QCOMPARE(w.scene, third);
        QTemporaryDir temp(QDir::currentPath() + "/tabs-tests-XXXXXX");
        const QString a = temp.filePath("assembly-A.bagua.json"), b = temp.filePath("assembly-B.bagua.json");
        tabs->setCurrentIndex(0);
        QVERIFY(w.savePath(a));
        tabs->setCurrentIndex(2);
        QVERIFY(w.savePath(b));
        Object changed = *second->current();
        changed.position = {4, 2, 1};
        second->edit(changed, "edit B");
        tabs->setCurrentIndex(0);
        QVERIFY(w.openPath(b));
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(w.scene, second);
        QCOMPARE(second->current()->position, QVector3D(4, 2, 1));
        QVERIFY(!second->undo.isClean());
        QCOMPARE(tabs->tabText(2), QString("assembly-B.bagua.json *"));
        bool warned = false;
        QTimer::singleShot(0, &w, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warned = true;
                box->button(QMessageBox::Ok)->click();
            }
        });
        QVERIFY(!w.savePath(a));
        QVERIFY(warned);
        Scene saved;
        QString error;
        QVERIFY(saved.load(a, &error));
        QCOMPARE(saved.objects.size(), 4);
        tabs->setCurrentIndex(0);
        QDir().mkpath(QDir::currentPath() + "/../artifacts");
        w.grab().save(QDir::currentPath() + "/../artifacts/editor-tabs-preview.png");
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            w.scene->undo.setClean();
        }
        // Overflow and the drop-down list work with many documents and one GPU viewport.
        for (int i = 0; i < 20; ++i)
            w.newScene();
        QCOMPARE(tabs->count(), 23);
        QCOMPARE(w.findChildren<Viewport *>().size(), 1);
        auto *list = w.findChild<QToolButton *>("sceneListButton");
        QMetaObject::invokeMethod(list->menu(), "aboutToShow", Qt::DirectConnection);
        QCOMPARE(list->menu()->actions().size(), 23);
        list->menu()->actions().first()->trigger();
        QCOMPARE(w.scene, first);
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(tabs->currentIndex(), 1);
        QTest::keyClick(viewport, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY(w.close());
    }
    void sceneTabClosingAndExitProtection() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *tabs = w.findChild<QTabBar *>("sceneTabs");
        QTemporaryDir temp(QDir::currentPath() + "/closing-tests-XXXXXX");
        const QString path = temp.filePath("saved.bagua.json");
        w.newScene();
        w.scene->create(Shape::Box);
        QVERIFY(w.savePath(path));
        w.scene->create(Shape::Sphere);
        w.newScene();
        auto *active = w.scene;
        active->create(Shape::Cylinder);
        auto answer = [&](QMessageBox::StandardButton button) {
            QTimer::singleShot(0, &w, [button] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    box->button(button)->click();
            });
        };
        answer(QMessageBox::Cancel);
        QVERIFY(!w.closeScene(1));
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(w.scene, active);
        answer(QMessageBox::Save);
        QVERIFY(w.closeScene(1));
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(w.scene, active);
        Scene saved;
        QString error;
        QVERIFY(saved.load(path, &error));
        QCOMPARE(saved.objects.size(), 2);
        answer(QMessageBox::Discard);
        QVERIFY(w.closeScene(1));
        QCOMPARE(tabs->count(), 1);
        QVERIFY(w.closeScene(0));
        QCOMPARE(tabs->count(), 1);
        QVERIFY(w.scene->objects.isEmpty());
        w.scene->create(Shape::Box);
        auto *before = w.scene;
        w.newScene();
        w.scene->create(Shape::Sphere);
        auto *current = w.scene;
        QTimer::singleShot(0, &w, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::Discard)->click();
            answer(QMessageBox::Cancel);
        });
        QVERIFY(!w.close());
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(before->objects.size(), 1);
        QCOMPARE(w.scene, current);
        for (int i = 0; i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            w.scene->undo.setClean();
        }
        w.viewport->setFocus();
        QTest::keyClick(w.viewport, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(tabs->count(), 1);
        QVERIFY(w.close());
    }
    void rightMouseFlyCamera() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *v = w.viewport;
        v->setFocus();
        v->target = {0, 0, 0};
        v->yaw = 0;
        v->pitch = 0;
        v->distance = 10;
        v->setTool(Tool::Scale);
        const QJsonArray objects = w.scene->json().value("objects").toArray();
        const int undoIndex = w.scene->undo.index();
        const auto initial = v->target;
        // Idle W remains an editing shortcut and never moves the camera.
        QTest::keyClick(v, Qt::Key_W);
        QCOMPARE(v->tool, Tool::Move);
        QCOMPARE(v->target, initial);
        v->setTool(Tool::Scale);
        QTest::mousePress(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        const std::array<Qt::Key, 6> keys = {Qt::Key_W, Qt::Key_S, Qt::Key_A, Qt::Key_D, Qt::Key_Q, Qt::Key_E};
        const std::array<QVector3D, 6> directions = {QVector3D(0, 0, -1), QVector3D(0, 0, 1),  QVector3D(-1, 0, 0),
                                                     QVector3D(1, 0, 0),  QVector3D(0, -1, 0), QVector3D(0, 1, 0)};
        for (int i = 0; i < 6; ++i) {
            const auto before = v->target;
            QTest::keyPress(v, keys[i]);
            QTRY_VERIFY_WITH_TIMEOUT(QVector3D::dotProduct(v->target - before, directions[i]) > .03f, 1500);
            QTest::keyRelease(v, keys[i]);
            const auto delta = v->target - before;
            QVERIFY(QVector3D::crossProduct(delta, directions[i]).length() < 1e-4f);
            const auto stopped = v->target;
            QTest::qWait(40);
            QCOMPARE(v->target, stopped);
            QCOMPARE(v->tool, Tool::Scale);
        }
        // Both held keys contribute, without waiting for OS key repeat.
        auto before = v->target;
        QTest::keyPress(v, Qt::Key_W);
        QTest::keyPress(v, Qt::Key_D);
        QTRY_VERIFY_WITH_TIMEOUT(v->target.x() > before.x() + .02f && v->target.z() < before.z() - .02f, 1500);
        QTest::keyRelease(v, Qt::Key_W);
        QTest::keyRelease(v, Qt::Key_D);
        const auto eyeBefore = v->target + QVector3D(0, 0, v->distance);
        moveMouse(v, {300, 220}, Qt::RightButton);
        constexpr float radians = 3.14159265358979323846f / 180;
        const auto eyeAfter = v->target + v->distance * QVector3D(sinf(v->yaw * radians) * cosf(v->pitch * radians),
                                                                  sinf(v->pitch * radians),
                                                                  cosf(v->yaw * radians) * cosf(v->pitch * radians));
        QVERIFY((eyeAfter - eyeBefore).length() < 1e-4f);
        QVERIFY(v->yaw != 0);
        QVERIFY(v->pitch != 0);
        const float oldDistance = v->distance;
        QWheelEvent wheel(QPointF(300, 220), QPointF(v->mapToGlobal(QPoint(300, 220))), QPoint(), QPoint(0, 120),
                          Qt::RightButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &wheel);
        QCOMPARE(v->distance, oldDistance);
        QTest::keyPress(v, Qt::Key_W);
        QTest::qWait(50);
        QTest::mouseRelease(v, Qt::RightButton, Qt::NoModifier, {300, 220});
        before = v->target;
        QTest::qWait(60);
        QCOMPARE(v->target, before);
        QTest::keyRelease(v, Qt::Key_W);
        QTest::keyClick(v, Qt::Key_E);
        QCOMPARE(v->tool, Tool::Rotate);
        // Losing keyboard focus immediately stops a held navigation key.
        QTest::mousePress(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        before = v->target;
        QTest::keyPress(v, Qt::Key_W);
        QTRY_VERIFY_WITH_TIMEOUT((v->target - before).length() > .03f, 1500);
        w.findChild<QLineEdit *>("objectName")->setFocus();
        before = v->target;
        QTest::qWait(60);
        QCOMPARE(v->target, before);
        QTest::keyRelease(v, Qt::Key_W);
        QTest::mouseRelease(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        // A tab switch also stops flight even if the right button is still held.
        v->setFocus();
        QTest::mousePress(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        QTest::keyPress(v, Qt::Key_W);
        QTest::qWait(50);
        auto *first = w.scene;
        w.newScene();
        before = v->target;
        QTest::qWait(60);
        QCOMPARE(v->target, before);
        QTest::keyRelease(v, Qt::Key_W);
        QTest::mouseRelease(v, Qt::RightButton, Qt::NoModifier, {250, 200});
        w.findChild<QTabBar *>("sceneTabs")->setCurrentIndex(0);
        before = v->target;
        QTest::qWait(60);
        QCOMPARE(v->target, before);
        QCOMPARE(first->json().value("objects").toArray(), objects);
        QCOMPARE(first->undo.index(), undoIndex);
        QVERIFY(w.close());
    }
};
int main(int argc, char **argv) {
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv);
    bagua::applyEditorTheme(app);
    EditorTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "EditorTests.moc"
