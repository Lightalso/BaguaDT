#pragma once
#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>
#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;

namespace bagua {
class Scene;

struct VehicleWheelSpec {
    bool enabled = true;
    QString spindle, rim, tire;
};
struct VehicleAssemblyRequest {
    QString chassis;
    std::array<VehicleWheelSpec, 4> wheels;
    QVector<QPair<QString, QString>> loosePairs;
    double suspensionStrength = 1e5;
    double suspensionDamping = 4753.8923;
    double suspensionLower = -.15;
    double suspensionUpper = .15;
    double springStiffness = 45374.0375;
    double springRest = .46266297384269583;
    double driveSpeed = 2.0;
    int beadAnchors = 39;
    double beadRadius = .25;
    double beadStiffness = 1e8;
    bool enableHsc = true;
    bool applyHscPreset = true;
};

// Non-modal GUI path for the HSC vehicle example: choose the chassis plus four
// spindle/rim/tire triples, then create suspension, motor and bead links in one
// undoable assembly step. Loose tire pairs are optional.
class VehicleAssemblyDialog : public QDialog {
    Q_OBJECT
public:
    explicit VehicleAssemblyDialog(const Scene &scene, QWidget *parent = nullptr);
    VehicleAssemblyRequest request() const;

protected:
    void accept() override;

private:
    void autoMatch();
    static void addObjectItems(QComboBox *combo, const Scene &scene);

    const Scene &scene;
    QComboBox *chassisCombo = nullptr;
    std::array<QCheckBox *, 4> wheelEnabled{};
    std::array<QComboBox *, 4> spindleCombos{};
    std::array<QComboBox *, 4> rimCombos{};
    std::array<QComboBox *, 4> tireCombos{};
    QTableWidget *wheelTable = nullptr;
    std::array<QCheckBox *, 3> looseEnabled{};
    std::array<QComboBox *, 3> looseRimCombos{};
    std::array<QComboBox *, 3> looseTireCombos{};
    QTableWidget *looseTable = nullptr;
    QDoubleSpinBox *suspensionStrength = nullptr;
    QDoubleSpinBox *suspensionDamping = nullptr;
    QDoubleSpinBox *suspensionLower = nullptr;
    QDoubleSpinBox *suspensionUpper = nullptr;
    QDoubleSpinBox *springStiffness = nullptr;
    QDoubleSpinBox *springRest = nullptr;
    QDoubleSpinBox *driveSpeed = nullptr;
    QSpinBox *beadAnchors = nullptr;
    QDoubleSpinBox *beadRadius = nullptr;
    QDoubleSpinBox *beadStiffness = nullptr;
    QCheckBox *enableHsc = nullptr;
    QCheckBox *applyHscPreset = nullptr;
};
} // namespace bagua
