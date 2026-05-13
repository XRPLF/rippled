/**
 * Shared type definitions for the doc-agent.
 */

export type Severity = 'warning' | 'suggestion';

export interface ReviewIssue {
  file: string;
  line: number;
  severity: Severity;
  message: string;
  suggestedDoc?: string;
}

export interface FileReviewResult {
  file: string;
  summary: string;
  issues: ReviewIssue[];
}

export interface ReviewOutput {
  summary: string;
  issues: Array<{
    file?: string;
    line: number;
    severity: Severity;
    message: string;
    suggested_doc?: string;
  }>;
}

export interface GitRange {
  base: string;
  head: string;
}

export type AgentMode = 'document' | 'review';
