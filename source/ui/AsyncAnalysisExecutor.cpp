#include "ui/AsyncAnalysisExecutor.hpp"

namespace omniframe::ui {

AsyncAnalysisExecutor::AsyncAnalysisExecutor(std::function<std::uint64_t()> revisionProvider,
                                             QObject* parent)
    : QObject(parent),
      m_revisionProvider(std::move(revisionProvider)) {}

AsyncAnalysisExecutor::~AsyncAnalysisExecutor() {
    try {
        cancelAndWait(5000, false);
    } catch (...) {
    }
}

void AsyncAnalysisExecutor::setRevisionProvider(std::function<std::uint64_t()> revisionProvider) {
    m_revisionProvider = std::move(revisionProvider);
}

std::uint64_t AsyncAnalysisExecutor::currentRevision() const noexcept {
    if (!m_revisionProvider) {
        return 0;
    }
    try {
        return m_revisionProvider();
    } catch (...) {
        return 0;
    }
}

void AsyncAnalysisExecutor::requestCancel() {
    m_stopSource.request_stop();
}

void AsyncAnalysisExecutor::requestCancelForJob(std::uint64_t jobId) {
    if (jobId != 0 && m_activeJobId == jobId) {
        m_stopSource.request_stop();
    }
}

void AsyncAnalysisExecutor::cancelAndWait(unsigned long timeoutMs, bool emitSignals) {
    const bool wasBusy = m_busy;
    if (m_activeAbandonFn) {
        m_activeAbandonFn();
    }
    m_stopSource.request_stop();
    if (m_activeThread && m_activeThread->isRunning()) {
        m_activeThread->wait(timeoutMs);
    }
    if (m_activeThread && !m_activeThread->isRunning()) {
        m_activeThread.clear();
    }
    m_busy = false;
    m_activeJobId = 0;
    m_activeAbandonFn = {};
    m_activeFinalizeFn = {};
    if (emitSignals && wasBusy) {
        emit busyChanged(false);
    }
}

void AsyncAnalysisExecutor::abandonAndWaitForJob(std::uint64_t jobId, unsigned long timeoutMs) {
    if (jobId == 0 || m_activeJobId != jobId) {
        return;
    }
    if (m_activeAbandonFn) {
        m_activeAbandonFn();
    }
    m_stopSource.request_stop();
    if (m_activeThread && m_activeThread->isRunning()) {
        m_activeThread->wait(timeoutMs);
    }
    if ((!m_activeThread || !m_activeThread->isRunning()) && m_activeFinalizeFn) {
        auto finalizeCopy = std::move(m_activeFinalizeFn);
        m_activeFinalizeFn = {};
        m_activeAbandonFn = {};
        finalizeCopy();
    }
}

} // namespace omniframe::ui
