#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>
#include <utility>

namespace omniframe::ui {

/**
 * @brief Executor unificado e mutuamente exclusivo para análises assíncronas em worker threads Qt.
 *
 * Centraliza ciclo de vida de `QThread` (limpeza incondicional via `QThread::finished -> deleteLater`),
 * captura de exceções (`try/catch`), cancelamento cooperativo via `std::stop_token` e rastreamento de
 * revisão do modelo entre `MainWindow` e `AnalysisSuiteDialog`.
 */
class AsyncAnalysisExecutor final : public QObject {
    Q_OBJECT

public:
    explicit AsyncAnalysisExecutor(std::function<std::uint64_t()> revisionProvider = {},
                                   QObject* parent = nullptr);
    ~AsyncAnalysisExecutor() override;

    void setRevisionProvider(std::function<std::uint64_t()> revisionProvider);
    [[nodiscard]] std::uint64_t currentRevision() const noexcept;

    [[nodiscard]] bool isBusy() const noexcept { return m_busy; }
    [[nodiscard]] std::uint64_t activeJobId() const noexcept { return m_activeJobId; }

    /**
     * @brief Solicita cancelamento cooperativo ao job ativo (ou apenas se corresponder a `jobId`).
     */
    void requestCancel();
    void requestCancelForJob(std::uint64_t jobId);

    /**
     * @brief Solicita cancelamento e aguarda o encerramento da thread ativa sem invocar callbacks de UI.
     */
    void cancelAndWait(unsigned long timeoutMs = 5000, bool emitSignals = true);
    void abandonAndWaitForJob(std::uint64_t jobId, unsigned long timeoutMs = 5000);

    /**
     * @brief Submete trabalho assíncrono mutuamente exclusivo.
     *
     * @param contextObj Objeto Qt cuja sobrevivência protege a entrega do resultado na GUI thread.
     * @param workFn Invocável `(std::stop_token) -> ResultT` executado em thread dedicada.
     * @param doneFn Invocável `(const ResultT&, std::uint64_t startRev, bool wasStopped)` na GUI thread.
     * @param errFn Invocável `(const QString&)` caso uma exceção seja capturada no worker.
     * @param jobIdOut Identificador monotônico atribuído ao job aceito (opcional).
     * @return `false` se já existir outro job em execução no executor.
     */
    template <typename ResultT, typename WorkFn, typename DoneFn, typename ErrorFn>
    bool submit(QObject* contextObj,
                WorkFn&& workFn,
                DoneFn&& doneFn,
                ErrorFn&& errFn,
                std::uint64_t* jobIdOut = nullptr) {
        if (m_busy) {
            return false;
        }

        m_stopSource = std::stop_source{};
        const std::stop_token stopToken = m_stopSource.get_token();
        const std::uint64_t startRev = currentRevision();
        const std::uint64_t jobId = ++m_nextJobId;

        struct SharedPayload {
            ResultT result{};
            bool hadException = false;
            QString exceptionMessage;
            bool abandoned = false;
            bool finalized = false;
        };

        auto payload = std::make_shared<SharedPayload>();
        auto workPtr = std::make_shared<std::decay_t<WorkFn>>(std::forward<WorkFn>(workFn));

        QThread* worker = QThread::create([payload, workPtr, stopToken]() noexcept {
            try {
                payload->result = (*workPtr)(stopToken);
            } catch (const std::exception& ex) {
                payload->hadException = true;
                try {
                    payload->exceptionMessage = QString::fromUtf8(ex.what());
                } catch (...) {
                    payload->exceptionMessage = QStringLiteral("Exceção durante execução assíncrona.");
                }
            } catch (...) {
                payload->hadException = true;
                try {
                    payload->exceptionMessage = QStringLiteral("Exceção desconhecida durante execução assíncrona.");
                } catch (...) {
                }
            }
        });

        auto donePtr = std::make_shared<std::decay_t<DoneFn>>(std::forward<DoneFn>(doneFn));
        auto errPtr = std::make_shared<std::decay_t<ErrorFn>>(std::forward<ErrorFn>(errFn));
        const QPointer<QObject> ctxPtr(contextObj);
        const QPointer<AsyncAnalysisExecutor> execPtr(this);

        auto finalize = [execPtr,
                         jobId,
                         startRev,
                         stopToken,
                         payload,
                         ctxPtr,
                         donePtr,
                         errPtr]() {
            if (payload->finalized) {
                return;
            }
            payload->finalized = true;

            if (execPtr) {
                if (execPtr->m_activeJobId == jobId) {
                    execPtr->m_busy = false;
                    execPtr->m_activeJobId = 0;
                    execPtr->m_activeThread.clear();
                    execPtr->m_activeAbandonFn = {};
                    execPtr->m_activeFinalizeFn = {};
                    emit execPtr->busyChanged(false);
                }
            }

            const std::uint64_t latestRev = execPtr ? execPtr->currentRevision() : startRev;
            const bool stale = (startRev != latestRev);
            const bool succeeded = (!payload->hadException && !stale && !payload->abandoned);

            if (!payload->abandoned && ctxPtr) {
                if (payload->hadException) {
                    (*errPtr)(payload->exceptionMessage);
                } else {
                    (*donePtr)(payload->result, startRev, stopToken.stop_requested());
                }
            }

            if (execPtr && !payload->abandoned) {
                emit execPtr->jobCompleted(jobId, succeeded);
            }
        };

        m_busy = true;
        m_activeJobId = jobId;
        m_activeThread = worker;
        m_activeAbandonFn = [payload]() { payload->abandoned = true; };
        m_activeFinalizeFn = finalize;
        if (jobIdOut) {
            *jobIdOut = jobId;
        }

        // [AUD-015] Limpeza incondicional do QThread independente do tempo de vida de contextObj.
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(worker, &QThread::finished, this, [finalize]() { finalize(); });

        emit busyChanged(true);
        worker->start();
        return true;
    }

signals:
    void busyChanged(bool busy);
    void jobCompleted(std::uint64_t jobId, bool succeeded);

private:
    std::function<std::uint64_t()> m_revisionProvider;
    std::stop_source m_stopSource;
    QPointer<QThread> m_activeThread;
    std::function<void()> m_activeAbandonFn;
    std::function<void()> m_activeFinalizeFn;
    std::uint64_t m_activeJobId = 0;
    std::uint64_t m_nextJobId = 0;
    bool m_busy = false;
};

} // namespace omniframe::ui
