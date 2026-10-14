#include "ServicePage.h"
#include "../../application/services/ServiceDefinitionValidator.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QProcess>
#include <QTextDocument>

namespace devhub::presentation::services {
ServicePage::ServicePage(application::services::IServiceDefinitionRepository& repository,
    application::services::ServiceCoordinator& coordinator, QWidget* parent)
    : QWidget(parent), m_repository(repository), m_coordinator(coordinator) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(24,20,24,24);
    auto* title = new QLabel(tr("Services"),this); title->setStyleSheet(QStringLiteral("font-size:18pt;font-weight:600;"));
    m_status = new QLabel(tr("Select a workspace to manage services."),this); layout->addWidget(title); layout->addWidget(m_status);
    auto* actions = new QHBoxLayout();
    for (const auto& label : {tr("Add"),tr("Edit"),tr("Remove"),tr("Start"),tr("Stop"),tr("Restart")}) {
        auto* button = new QPushButton(label,this); actions->addWidget(button);
        if (label==tr("Add")) connect(button,&QPushButton::clicked,this,[this]{editDefinition();});
        else if (label==tr("Edit")) connect(button,&QPushButton::clicked,this,[this]{
            if (!m_list->currentIndex().isValid()) return;
            auto rows=m_repository.list(m_workspaceId); if (!rows) return;
            const auto id=m_model->serviceId(m_list->currentIndex());
            for (const auto& item:rows.value()) if (item.id==id) { editDefinition(item); break; }
        });
        else if (label==tr("Remove")) connect(button,&QPushButton::clicked,this,[this]{
            if (!m_list->currentIndex().isValid()) return;
            const QString id=m_model->serviceId(m_list->currentIndex());
            if (QMessageBox::question(this,tr("Remove service"),tr("Remove this service definition?"))!=QMessageBox::Yes) return;
            auto removed=m_repository.remove(id,m_workspaceId); if (!removed) QMessageBox::warning(this,tr("Remove failed"),removed.error().userMessage); reload();
        });
        else if (label==tr("Start")) connect(button,&QPushButton::clicked,this,[this]{
            if (!m_list->currentIndex().isValid()) return;
            const QString id=m_model->serviceId(m_list->currentIndex());
            auto rows=m_repository.list(m_workspaceId); if (!rows) return;
            for (const auto& item:rows.value()) if (item.id==id) { auto started=m_coordinator.start(item,m_root); if (!started) QMessageBox::warning(this,tr("Start failed"),started.error().userMessage); break; }
        });
        else if (label==tr("Stop")) connect(button,&QPushButton::clicked,this,[this]{if(m_list->currentIndex().isValid()){const auto r=m_coordinator.stop(m_model->serviceId(m_list->currentIndex()));if(!r)QMessageBox::warning(this,tr("Stop failed"),r.error().userMessage);}});
        else connect(button,&QPushButton::clicked,this,[this]{if(m_list->currentIndex().isValid()){const auto r=m_coordinator.restart(m_model->serviceId(m_list->currentIndex()),m_root);if(!r)QMessageBox::warning(this,tr("Restart failed"),r.error().userMessage);}});
    }
    auto* startAll=new QPushButton(tr("Start All"),this);auto* stopAll=new QPushButton(tr("Stop All"),this);
    actions->addWidget(startAll);actions->addWidget(stopAll);actions->addStretch(); layout->addLayout(actions);
    connect(startAll,&QPushButton::clicked,this,[this]{
        auto rows=m_repository.list(m_workspaceId);if(!rows){QMessageBox::warning(this,tr("Could not load services"),rows.error().userMessage);return;}
        m_coordinator.startAll(rows.value(),m_root,3);
    });
    connect(stopAll,&QPushButton::clicked,this,[this]{
        if(QMessageBox::question(this,tr("Stop all services"),tr("Stop all managed services in reverse start order?"))==QMessageBox::Yes)m_coordinator.stopAll();
    });
    auto* split=new QSplitter(Qt::Vertical,this); m_list=new QListView(split);m_list->setAccessibleName(tr("Managed services and status"));m_model=new ServiceListModel(this);m_list->setModel(m_model);
    m_output=new QPlainTextEdit(split); m_output->setAccessibleName(tr("Selected service output")); m_output->setReadOnly(true);
    m_output->document()->setMaximumBlockCount(3000);
    m_output->setPlaceholderText(tr("Service stdout and stderr appear here.")); split->addWidget(m_list); split->addWidget(m_output); layout->addWidget(split,1);
    connect(&m_coordinator,&application::services::ServiceCoordinator::stateChanged,this,[this](const QString&,const auto&){reload();});
    connect(&m_coordinator,&application::services::ServiceCoordinator::outputAvailable,this,[this](const QString& id,const QByteArray& bytes,bool error){
        m_output->appendPlainText(QStringLiteral("[%1] %2").arg(error?QStringLiteral("stderr"):QStringLiteral("stdout"),QString::fromUtf8(bytes)));
        Q_UNUSED(id);
    });
}
void ServicePage::setWorkspace(QString id,QString root){m_workspaceId=std::move(id);m_root=std::move(root);reload();}
void ServicePage::reload(){
    if(m_workspaceId.isEmpty()){m_model->setDefinitions({});m_model->setRuntimeStates({});m_status->setText(tr("Select a workspace to manage services."));return;}
    auto definitions=m_repository.list(m_workspaceId); if(!definitions){m_status->setText(definitions.error().userMessage);return;}
    m_model->setDefinitions(definitions.value());m_model->setRuntimeStates(m_coordinator.states());
    m_status->setText(tr("%1 service definition(s)").arg(definitions.value().size()));
}
void ServicePage::editDefinition(const std::optional<domain::services::ServiceDefinition>& existing){
    QDialog dialog(this); dialog.setWindowTitle(existing?tr("Edit service"):tr("Add service")); auto* form=new QFormLayout(&dialog);
    auto* name=new QLineEdit(existing?existing->name:QString(),&dialog); auto* program=new QLineEdit(existing?existing->program:QString(),&dialog);
    auto* arguments=new QLineEdit(existing?existing->arguments.join(QLatin1Char(' ')):QString(),&dialog);
    auto* working=new QLineEdit(existing?existing->workingDirectory:m_root,&dialog);
    auto* browse=new QPushButton(tr("Browse…"),&dialog); auto* programRow=new QHBoxLayout();programRow->addWidget(program);programRow->addWidget(browse);
    connect(browse,&QPushButton::clicked,&dialog,[this,program]{const auto path=QFileDialog::getOpenFileName(this,tr("Choose program"));if(!path.isEmpty())program->setText(path);});
    auto* env=new QLineEdit(existing?existing->environmentNames.join(QLatin1Char(',')):QString(),&dialog);
    auto* ports=new QLineEdit(&dialog); if(existing){QStringList values;for(auto p:existing->expectedPorts)values.append(QString::number(p));ports->setText(values.join(QLatin1Char(',')));}
    auto* shell=new QCheckBox(tr("Run through system shell (advanced)"),&dialog); shell->setChecked(existing&&existing->shellMode);
    auto* warning=new QLabel(tr("Shell mode interprets a command string. Prefer direct executable mode."),&dialog);warning->setWordWrap(true);warning->setVisible(shell->isChecked());
    connect(shell,&QCheckBox::toggled,warning,&QWidget::setVisible);
    auto* restart=new QComboBox(&dialog);restart->addItem(tr("Never"),0);restart->addItem(tr("On failure"),1);restart->addItem(tr("Always"),2);
    auto* timeout=new QSpinBox(&dialog);timeout->setRange(100,120000);timeout->setValue(existing?existing->stopTimeoutMs:5000);timeout->setSuffix(tr(" ms"));
    form->addRow(tr("Name"),name);form->addRow(tr("Program"),programRow);form->addRow(tr("Arguments"),arguments);form->addRow(tr("Working directory"),working);
    env->setToolTip(tr("This field is saved as metadata only. Services inherit the full DevHub environment unchanged; the list does not restrict or validate variables."));
    form->addRow(tr("Environment names (metadata only; full parent environment inherited)"),env);form->addRow(tr("Expected ports"),ports);form->addRow(shell);form->addRow(warning);form->addRow(tr("Restart policy"),restart);form->addRow(tr("Stop timeout"),timeout);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    domain::services::ServiceDefinition def=existing.value_or(domain::services::ServiceDefinition{});
    if(def.id.isEmpty())def.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.workspaceId=m_workspaceId;def.name=name->text();def.program=program->text();def.arguments=QProcess::splitCommand(arguments->text());def.workingDirectory=working->text();def.shellMode=shell->isChecked();
    def.environmentNames=env->text().split(QLatin1Char(','),Qt::SkipEmptyParts);for(auto& value:def.environmentNames)value=value.trimmed();
    for(const auto& value:ports->text().split(QLatin1Char(','),Qt::SkipEmptyParts)){bool ok=false;const int port=value.trimmed().toInt(&ok);if(ok&&port>0&&port<=65535)def.expectedPorts.append(static_cast<quint16>(port));}
    def.restartPolicy=static_cast<domain::services::RestartPolicy>(restart->currentData().toInt());def.stopTimeoutMs=timeout->value();
    application::services::ServiceDefinitionValidator validator;auto checked=validator.validate(def,m_root);
    if(!checked){QMessageBox::warning(this,tr("Invalid service"),checked.error().userMessage);return;}
    auto saved=m_repository.save(checked.value());if(!saved)QMessageBox::warning(this,tr("Save failed"),saved.error().userMessage);reload();
}
}
