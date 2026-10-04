package com.localacr.demo

import com.localacr.LocalAcrFactory
import com.localacr.LocalAcrRecognizer
import com.localacr.OperationResult
import com.localacr.PrepareResult
import com.localacr.RecognitionError
import com.localacr.RecognitionListener
import com.localacr.RecognitionResult
import com.localacr.RecognitionState

class SharedLocalAcrDemoRecognizer(
    private val databasePath: String,
) : DemoRecognizer {
    private var recognizer: LocalAcrRecognizer? = null
    private var prepared = false
    private var pendingListener: DemoRecognitionListener? = null
    private var prepareErrorMessage: String? = null

    override fun prepare(): DemoOperationResult {
        if (prepared) {
            return DemoOperationResult.Success
        }

        val created = LocalAcrFactory.create(databasePath)
        val nextRecognizer = created.recognizer
        if (nextRecognizer == null) {
            return DemoOperationResult.Failure(created.error?.message ?: "Unable to create recognizer")
        }

        recognizer = nextRecognizer
        nextRecognizer.prepare { result -> onPrepared(result) }
        return DemoOperationResult.Success
    }

    override fun start(listener: DemoRecognitionListener): DemoOperationResult {
        if (prepareErrorMessage != null) {
            return DemoOperationResult.Failure(prepareErrorMessage!!)
        }
        pendingListener = listener
        if (!prepared) {
            return DemoOperationResult.Success
        }
        return startPreparedRecognizer(listener)
    }

    override fun stop(): DemoOperationResult {
        pendingListener = null
        val current = recognizer ?: return DemoOperationResult.Success
        current.stop { }
        return DemoOperationResult.Success
    }

    private fun onPrepared(result: PrepareResult) {
        val error = result.error
        if (error != null) {
            prepareErrorMessage = error.message
            pendingListener?.onError(error)
            return
        }
        prepared = true
        val listener = pendingListener ?: return
        val startResult = startPreparedRecognizer(listener)
        if (startResult is DemoOperationResult.Failure) {
            listener.onError(
                RecognitionError(
                    code = com.localacr.RecognitionErrorCode.InvalidState,
                    message = startResult.message,
                    recoverable = true,
                ),
            )
        }
    }

    private fun startPreparedRecognizer(listener: DemoRecognitionListener): DemoOperationResult {
        val current = recognizer ?: return DemoOperationResult.Failure("Recognizer is not prepared")
        var operationError: RecognitionError? = null
        current.start(
            object : RecognitionListener {
                override fun onStateChanged(state: RecognitionState) = Unit
                override fun onRecognized(result: RecognitionResult) {
                    listener.onRecognized(result)
                }

                override fun onError(error: RecognitionError) {
                    listener.onError(error)
                }
            },
        ) { result: OperationResult ->
            operationError = result.error
            // Completion is dispatched asynchronously, so the synchronous return below cannot see it.
            result.error?.let { listener.onError(it) }
        }
        return operationError?.let { DemoOperationResult.Failure(it.message) } ?: DemoOperationResult.Success
    }
}
